/*******************************************************************************
 * Title       : qdda - quick & dirty dedup analyzer
 * Description : Checks files or block devices for duplicate blocks
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * More info   : http://outrun.nl/wiki/qdda
 * -----------------------------------------------------------------------------
 */

#include "qdda.h"
using namespace std;

/*******************************************************************************
 * Initialization - globals
 ******************************************************************************/

sqlitedb dbase;                          // global access to database
std::ofstream  o_debug;                  // Debug stream, open with -D option
std::ofstream  o_verbose("/dev/stdout"); // Progress stream, disable with -q option

// option parameters/switches
// bool   p_dryrun     = false;          // don't update database
bool   p_nocompress = false;          // disable compression analysis
bool   p_noreport   = false;          // disable results report
bool   p_keep       = false;          // delete database unless
bool   p_dump       = false;          // dump block offsets and hashes
u_long p_blksz      = default_blksz;  // default blocksize
u_long p_bandwidth  = default_bw;     // default bandwidth

/*******************************************************************************
 * Usage
 ******************************************************************************/

const char * version_text  = "qdda " progversion " - the quick & dirty dedupe analyzer\n";
const char * version_info  = "build date: " builddate R"(

Copyright (C) 2017 Bart Sjerps <bart@outrun.nl>
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
)";

string usage_text = R"(
Usage: qdda [-D] [-B blksize] [-b <bandw>] [-c] [-d] [-f <dbpath>] [-k] [-p gb] [-r] [-v] [file list]
  -D (debug)       : show lots of annoying debug info
  -B <blksize_kb>  : set blocksize to blksize_kb kilobytes
  -b <bandw>       : throttle bandwidth in MB/s (default 200, set to 0 to disable throttling)
  -c (no-Compress) : skip compression estimate
  -d (dump)        : dump block offsets and hashes
  -k (keep)        : keep existing database when feeding more data
  -f <db filepath> : specify alternative database location (default /var/tmp/qdda.db)
  -p (perftest)    : run raw performance test with 1GB random data
  -q (quiet)       : don't show progress indicator or intermediate results
  -r (no-Report)   : don't show report
  -v (version)     : print version and copyright info

qdda is safe to run even on files/devices that are in use. It opens streams read-only and cannot modify any files.
It writes to a database file that needs to be either newly created or a pre-existing SQLite3 database.
It can remove the database file but ONLY if it is an SQLite3 file (it tests the file magic).
For added safety you may run qdda as a non-privileged user and only open named/unnamed pipes or standard input (stdin), i.e.
unix pipe:  sudo cat /dev/disk | qdda <options>
named pipe: mkfifo p; sudo cat /dev/disk > p & qdda <options> p

total               = Total data blocks scanned
free                = Free (zeroed) space
used                = Used (non-zero) space
unique              = Blocks that only appear once (non-dedupable)
deduped 2x          = Blocks that appear exactly 2 times
deduped 3x          = Blocks that appear exactly 3 times
deduped 4x          = Blocks that appear exactly 4 times
deduped >4x         = Blocks that appear 5 times or more
deduped total       = Required capacity after deduplication
stream compressed   = Sum of deduped block bytes compressed with LZ4
compress buckets 2k = Compressed blocks that fit in 2k slots (4 per 8K block)
compress buckets 4k = Compressed blocks that fit in 4k slots (2 per 8K block)
compress buckets 8k = Remaining blocks (not compressed, require full 8K block)
total compressed    = Blocks required to fit all compress buckets (overall required capacity)

Summary:
percentage used     = Percentage used/total (logical capacity, before optimization)
percentage free     = Percentage free/total (logical capacity, before optimization)
deduplication ratio = capacity used / deduped
compression ratio   = capacity deduped / required (bucket compression)
thin ratio          = capacity total / used
combined            = all ratios combined (total possible optimization efficiency)
raw capacity        = Total scanned capacity (same as total)
net capacity        = Total required capacity (same as required)

More info: http://outrun.nl/wiki/qdda
)";

void showversion() { cout << version_text << version_info << endl; exit(0); }
void showusage()   { cout << version_text << usage_text << endl; }

/****************************************************************************
* Formatting & printing
****************************************************************************/

std::string printPRC(float perc) { 
  stringstream ss;
  ss << setfill(' ') << setprecision(4) << perc << " %"; 
  return ss.str(); 
}

std::string printMIB(u_long bytes)   {
  stringstream ss;
  ss << setfill(' ') << fixed << setprecision(2) << setw(4) << (float)bytes / mebibyte << " MiB";
  return ss.str(); 
}
std::string printBLK(u_long blocks)   {
  stringstream ss;
  ss << setfill(' ') << to_string(blocks) + " blocks";
  return ss.str(); 
}

// class blockval stuff
u_long blockval::blocksz = 0; // static initialization outside class
void blockval::setblocksize(u_int b) { blocksz = b; }
void blockval::operator=(u_long x) { blks = x; }

// reset manipulators of an ostream (copy default flags from null)
void os_reset(std::ostream& os = cout) { os.copyfmt(std::ios(NULL)); }

void showprogress(int newfnum, u_long blocks, u_long bytes) {
  static auto   prev  = clocknow;
  auto          now   = clocknow;
  long          svctm = 0; // service time to read <chunk> blocks
  static int    fnum  = 0; // keep track of previous and new files
  if(fnum!=newfnum) { prev=now ; fnum=newfnum ; } // initialize with new file

  svctm = clockdiff(now,prev);

  u_long bw = (svctm==0) ? 0 : bytes/svctm; // bytes per sec
  stringstream ss;
  ss << "File " << setfill('0') << setw(2) << fnum << ", "
     << blocks << " blocks, " 
     << bytes/mebibyte
     << " MiB processed, "
     << bw << " MB/s                 " ;
  o_verbose << ss.str(); 
  for(u_int i=0;i<ss.str().length();i++) o_verbose << '\b';
  o_verbose << flush;
}

/*
 * SQL group by val: (TBD)
 * select v,count(v),count(v)*blocksize from kv where k!=0 group by v;
*/

// Print stats report
void report() {
  dbase.open();                     // open db if it wasn't open already
  os_reset();                       // reset cout state

  // get all values from database
  auto     blocksize  = dbase.select_long("select blksz from metadata");                         // Blocksize used when scanning
  blockval blk_total  = dbase.select_long("select sum(v) from kv");                              // Total scanned blocks
  blockval blk_free   = dbase.select_long("select v from kv where k=0");                         // Total zero blocks
  blockval blk_used   = dbase.select_long("select sum(v) from kv where k!=0");                   // Total non-zero blocks
  blockval blk_dedup  = dbase.select_long("select count(*) from kv where k!=0");                 // Unique hashes (deduped)
  blockval blk_uniq   = dbase.select_long("select count(v) from kv where k!=0 and v=1");         // Hashes with count=1 (non-dedupeable data)
//blockval blk_multi  = dbase.select_long("select count(v) from kv where k!=0 and v>1");         // Hashes with count>1 (dedupable data)
  blockval blk_count2 = dbase.select_long("select count(v) from kv where k!=0 and v=2");         // Hashes with count=2 (dedupable data)
  blockval blk_count3 = dbase.select_long("select count(v) from kv where k!=0 and v=3");         // Hashes with count=3 (dedupable data)
  blockval blk_count4 = dbase.select_long("select count(v) from kv where k!=0 and v=4");         // Hashes with count=4 (dedupable data)
  blockval blk_counth = dbase.select_long("select count(v) from kv where k!=0 and v>4");         // Hashes with count=5 (dedupable data)
  auto     bytes_comp = dbase.select_long("select sum(b)   from kv where k!=0");                 // Total bytes after full compression
  auto     buckets_2k = dbase.select_long("select count(*) from kv where b>0    and b<=2048");   // Blocks that compress into 2k
  auto     buckets_4k = dbase.select_long("select count(*) from kv where b>2048 and b<=4096");   // Blocks that compress into 2k+1 - 4k
  auto     buckets_8k = dbase.select_long("select count(*) from kv where b>4096");               // Blocks that don't fit in other buckets

  blk_total.setblocksize(blocksize);

  // calc compression buckets
  u_long blk_2k = buckets_2k * 2048 / blocksize;
  u_long blk_4k = buckets_4k * 4096 / blocksize;
  u_long blk_8k = buckets_8k * 8192 / blocksize;

  blockval blk_rq = blk_2k + blk_4k + blk_8k; // required blocks after bucket compression

  // calc ratios - divide by zero results in value 0
  float comp_ratio   = 1 - safe_div (bytes_comp,blk_dedup.bytes()); // full compression ratio
  float dedup_ratio  = safe_div (blk_used,  blk_dedup);             // dedupe ratio
  float used_ratio   = safe_div (blk_used,  blk_total);             // % used vs free
  float thin_ratio   = safe_div (blk_total, blk_used);              // % free vs used (thin provisioning)
  float bucket_ratio = safe_div (blk_dedup, blk_rq);                // bucket compression ratio (sorted into slots)

  // dump the formatted report
  cout << setprecision(2) << fixed; // display float as xyz.ab
  cout    << "blocksize           = " << w1 << to_string(blocksize/1024) + " KiB"
  << endl << "total               = " << w1 << printMIB(blk_total.bytes())  << " (" << w2 << blk_total  << " blocks)"
  << endl << "free                = " << w1 << printMIB(blk_free.bytes() )  << " (" << w2 << blk_free   << " blocks)"
  << endl << "used                = " << w1 << printMIB(blk_used.bytes() )  << " (" << w2 << blk_used   << " blocks)"
  << endl << "unique              = " << w1 << printMIB(blk_uniq.bytes() )  << " (" << w2 << blk_uniq   << " blocks)"
  << endl << "deduped 2x          = " << w1 << printMIB(blk_count2.bytes()) << " (" << w2 << blk_count2 << " blocks)"
  << endl << "deduped 3x          = " << w1 << printMIB(blk_count3.bytes()) << " (" << w2 << blk_count3 << " blocks)"
  << endl << "deduped 4x          = " << w1 << printMIB(blk_count4.bytes()) << " (" << w2 << blk_count4 << " blocks)"
  << endl << "deduped >4x         = " << w1 << printMIB(blk_counth.bytes()) << " (" << w2 << blk_counth << " blocks)"
//<< endl << "deduped total       = " << w1 << printMIB(blk_multi.bytes())  << " (" << w2 << blk_multi  << " blocks)"
  << endl << "deduped total       = " << w1 << printMIB(blk_dedup.bytes())  << " (" << w2 << blk_dedup  << " blocks)"
  << endl << "stream compressed   = " << w1 << printMIB(bytes_comp)         << " (" << w2 << 100*comp_ratio  << " %)"
  << endl << "compress buckets 2k = " << w1 << printMIB(buckets_2k*2048)    << " (" << w2 << buckets_2k << " buckets)"
  << endl << "compress buckets 4k = " << w1 << printMIB(buckets_4k*4096)    << " (" << w2 << buckets_4k << " buckets)"
  << endl << "compress buckets 8k = " << w1 << printMIB(buckets_8k*8192)    << " (" << w2 << buckets_8k << " buckets)"
  << endl << "total compressed    = " << w1 << printMIB(blk_rq.bytes())     << " (" << w2 << blk_rq     << " blocks)"
  << endl << "                      " << w1 << "*** Summary ***"
  << endl << "percentage used     = " << w1 << to_string(100*used_ratio,2)     << " %"
  << endl << "percentage free     = " << w1 << to_string(100-100*used_ratio,2) << " %"
  << endl << "deduplication ratio = " << w1 << to_string(dedup_ratio,2)
  << endl << "compression ratio   = " << w1 << to_string(bucket_ratio,2)
  << endl << "thin ratio          = " << w1 << to_string(thin_ratio,2)
  << endl << "combined            = " << w1 << to_string(dedup_ratio*bucket_ratio*thin_ratio,2)
  << endl << "raw capacity        = " << w1 << printMIB(blk_total.bytes())
  << endl << "net capacity        = " << w1 << printMIB(blk_rq.bytes())
  << endl;
}

/****************************************************************************
* SQLite stuff
****************************************************************************/

sqlitedb::sqlitedb() {
  fn = dbpath;
  db = 0 ;
  stmt_get = NULL;
  stmt_ins = NULL;
  stmt_inc = NULL;
}

sqlitedb::~sqlitedb() {
  if(stmt_get) sqlite3_finalize(stmt_get);
  if(stmt_ins) sqlite3_finalize(stmt_ins);
  if(stmt_inc) sqlite3_finalize(stmt_inc);
  sqlite3_close(db); db = 0;
}

// Open database and create tables if needed
int sqlitedb::open() {
  const char * sql_get      = "select v from kv where k=?";
  const char * sql_insert   = "insert into kv(k,v,b) values (?,1,?)";
  const char * sql_increase = "update kv set v=v+1 where k=?";
  const char * pzTest;
  int          rc = 0;
  if(db) return 0;
  rc = sqlite3_open(fn.c_str(), &db);
  if(rc) {
    cerr << "DB filename: " << fn << ": " << (char *)sqlite3_errmsg(db) << endl ;
    die ("Can't open database");
  } else {
    sql(sql_optimize);
    sql(sql_schema);
    auto blksz = select_long("select blksz from metadata");
    if(!blksz) sql("insert into metadata (blksz) values (" + to_string(p_blksz) + ")");
    o_debug << "DB opened: " << fn << endl;
  }
  if(sqlite3_prepare_v2(db, sql_get,      strlen(sql_get),      &stmt_get, &pzTest)!=SQLITE_OK) die("SQL Prepare error");
  if(sqlite3_prepare_v2(db, sql_insert,   strlen(sql_insert),   &stmt_ins, &pzTest)!=SQLITE_OK) die("SQL Prepare error");
  if(sqlite3_prepare_v2(db, sql_increase, strlen(sql_increase), &stmt_inc, &pzTest)!=SQLITE_OK) die("SQL Prepare error");
  return(rc);
}

int sqlitedb::db_exists() {
  const char * magic = "SQLite format 3";
  char buf[32];
  ifstream f(fn.c_str());
  if (!f.good()) return 0;        // can't open file - nonexistent?
  f.seekg (0, f.beg);
  f.read (buf,strlen(magic));     // read the magic string
  buf[strlen(magic)]=0;           // force terminate with \0
  if(strcmp(magic,buf)) return 2; // compare with magic
  return 1; 
}

// delete file only if it is a SQLite 3 database
int sqlitedb::deletedb() {
  if (db) return 0; // need to close first
  int rc = db_exists();
  if (rc==1) {
    cout << "Deleting " << fn << endl;
    unlink(fn.c_str());
  } else if (rc==2) {
    cerr << "Not a database: " << fn << endl;
    die("Cannot delete database");
    return 0;
  }
  return 1;
}

/* pre-compiled statements (performance) */

// get v from kv table
u_long sqlitedb::hashcount(unsigned int hash) {
  u_long retval = 0;
  if(!stmt_get) die("Getval statement error");
  sqlite3_bind_int64(stmt_get, 1, hash);
  sqlite3_step(stmt_get);
  retval = sqlite3_column_int64(stmt_get, 0);
  sqlite3_reset(stmt_get);
  return retval;
}

// insert new hash and compressed byte count
int sqlitedb::hashinsert(u_int hash,u_int bytes) {
  if(!stmt_ins) die("Insert statement error");
  sqlite3_bind_int64(stmt_ins, 1, hash);
  sqlite3_bind_int64(stmt_ins, 2, bytes);
  sqlite3_step(stmt_ins);
  sqlite3_clear_bindings(stmt_ins);
  sqlite3_reset(stmt_ins);
  return 0;
}

// increase v for hash
int sqlitedb::hashincr(u_int hash) {
  if(!stmt_inc) die("Increase statement error");
  sqlite3_bind_int64(stmt_inc, 1, hash);
  sqlite3_step(stmt_inc);
  sqlite3_clear_bindings(stmt_inc);
  sqlite3_reset(stmt_inc);
  return 0;
}

/* non-optimized SQL, don't use inside long loops */

// Run SQL statement, ignore results (update/insert)
void sqlitedb::sql(const string& sql) {
  int rc=0;
  char * errmsg;
  rc = sqlite3_exec(db, sql.c_str(), 0, 0, &errmsg);
  if( rc != SQLITE_OK ) {
    cerr << "SQL error: " << errmsg << endl;
    cerr << "Query: " << sql << endl;
    die("Cannot execute SQL");
  }
}

// run SQL statement, return longint value (select)
u_long sqlitedb::select_long(const string& query) {
  int          rc      = 0;
  u_long       retval  = 0;
  sqlite3_stmt *stmt   = 0;
  const char   *pzTest = NULL;

  o_debug << "query=" << query << endl;
  if(db==0) die("Query on closed database");
  rc = sqlite3_prepare_v2(db, query.c_str(), strlen(query.c_str()), &stmt, &pzTest);
  if(rc!=SQLITE_OK) die("SQL prepare error: " + query);
  rc=sqlite3_step(stmt);
  
  if(rc==SQLITE_ROW) { 
    retval = sqlite3_column_int64(stmt, 0);
    rc=sqlite3_step(stmt);
    if(rc!=SQLITE_DONE) die("SQL: Multiple return values for " + query);
  }
  sqlite3_finalize(stmt);
  return retval;
}

/****************************************************************************
* Functions
****************************************************************************/

// get crc32 hash or 0 if block is zero
u_long blockhash(const char * buf,const int size) {
  static int f = 0;
  static char zerobuf[max_blocksize];               // allocate static buffer (once)
  if(memcmp (buf,zerobuf,size)==0) return 0;        // return 0 for zero block
  if(!f) { memset(zerobuf,0,max_blocksize); f=1; }  // force hash=0 for zero block
  return crc32(0, (const unsigned char*)buf, size); // get crc
}

// Get compressed bytes for a compressed block - lz4
u_int compress(const char * src,const int size) {
  if(p_nocompress==true) return size;   // if compression is disabled, return blocksize
  static char buf[max_blocksize+1024];  // compressed block may be larger than uncompressed
  int result = LZ4_compress_default(src, buf, size, size+1024);
  if(result==0) die("Compression error");
  if(result>size) return size;          // don't compress if size is larger
  return result;
}

// Throttling mechanism - microsleep every <bytes> to adjust read MB/s
void throttle(const u_long& bandwidth,const u_long& bytes) {
  if(!bandwidth) return;                   // immediately return if we don't throttle
  static u_long cycletime = 0;             // cycle time (required microseconds per chunk)
  static auto    prev     = clocknow;      // keep t1 between calls
  auto           now      = clocknow;

  if(!cycletime) {                         // initialization when first called
    cycletime = bytes / bandwidth;
    o_verbose << "Throttling @ " << bandwidth << " MB/s, cycletime=" << cycletime << endl;
    return;
  };
  u_long servicetime = clockdiff(now,prev);
  if(cycletime > servicetime) usleep(cycletime - servicetime);
  prev=clocknow; // measure time diff at next call
}

// process file
void analyze(const char * filename) {
  ifstream f;              // file to be processed
  char buf[max_blocksize]; // buffer for calculating checksums etc
  static int filenum  = 0; // file counter for reporting
  u_int  hash         = 0; // crc32 will be stored here
  u_int  compressed   = 0; // compressed bytes
  u_long hashcount    = 0; // blocks with same hash
  u_long blocks       = 0; // block counter
  u_long bytecount    = 0; // byte counter

  f.open(filename);
  if (!f.is_open()) die("File open failed");
  filenum++;

  dbase.open(); // does nothing if already open

  // always use pre-existing blocksize to get consistent results
  auto blocksize = dbase.select_long("select blksz from metadata"); 

  dbase.sql("BEGIN TRANSACTION"); // avoid commit with every update - faster
  // TBD: we may want to commit every 10000 rows or so

  // loop through stream blocks
  while (!f.eof()) {
    memset(buf,0,blocksize);            // clear buffer
    f.read(buf,blocksize);              // read 1 block
    if(f.gcount()==0) break;            // zero bytes read = end reached
    hash  = blockhash(buf,blocksize);   // get the crc32 hash, or 0 if zero block
    hashcount = dbase.hashcount(hash);  // get value for this hash from database

    // dump hashes with -d option:
    if(p_dump) cout << setw(9) << setfill('0') << blocks << ","  << hex << setw(8) << hash << dec << endl;

    blocks++; bytecount += blocksize;                // increase counters
    if(hashcount) dbase.hashincr(hash);              // hash exists, increase counter
    else {                                           // hash does not exist
      if(hash) compressed = compress(buf,blocksize); // get compressed size
      else compressed = 0;                           // set to zero for zero block
      dbase.hashinsert(hash,compressed);             // insert new hash in database
    }
    if(blocks%updateinterval==0) showprogress(filenum,blocks,bytecount);          // progress indicator
    if(blocks%blockspercycle==0) throttle(p_bandwidth, blocksize*blockspercycle); // throttle bandwidth
  };
  dbase.sql("END TRANSACTION");
  showprogress(filenum,blocks,bytecount);
  o_verbose << endl;
};

// refuse to set blocksize too high
void set_blocksize(const char * in) {
  long bsize = atol(in);
  if(bsize*1024>max_blocksize) die("Blocksize exceeded");
  p_blksz=1024*bsize;
}

// test processing performance
void perftest(const long gigabytes = 1) {
  const long blocksize = 8192;
  const long totbytes  = gigabytes * 1024 * mebibyte;
  const long bufsize   = 16 * mebibyte;
  const long cycles    = totbytes / bufsize;
  const long blocks    = bufsize/blocksize;
  char      *testdata  = new char[bufsize];

  memset(testdata,0,bufsize);
  dbase.deletedb(); // use fresh database

  for(long i=0;i<bufsize;i++) testdata[i] = rand() % 256; // fill test buffer with random data

  // open /dev/zero to test ingest performance - not dependent on disk speeds
  ifstream f;
  f.open("/dev/zero");
  dbase.open();

  cout << fixed << setprecision(2);
  cout << "Test set:    " << w1 << cycles * blocks << " random 8k blocks (" << totbytes/mebibyte << " MiB)" << endl;

  auto t0 = clocknow;

  // test hashing performance
  for(long j=0;j<cycles;j++) for(long i=0;i<blocks;i++) blockhash(testdata + i*blocksize,blocksize);
  auto t1 = clocknow;

  // test compression performance
  for(long j=0;j<cycles;j++) for(long i=0;i<blocks;i++) compress(testdata + i*blocksize,blocksize);
  auto t2 = clocknow;

  // test sqlite insert performance
  dbase.sql("BEGIN TRANSACTION"); 
  for(long i=0;i<blocks*cycles;i++) dbase.hashinsert(i,blocksize);
  dbase.sql("END TRANSACTION");
  auto t3 = clocknow;

  // test input stream performance
  for(long j=0;j<cycles;j++) for(long i=0;i<blocks;i++) f.read (testdata + i*blocksize,blocksize);
  auto t4 = clocknow;

  // calculate timings
  u_long time_hash = clockdiff(t1,t0);
  u_long time_comp = clockdiff(t2,t1);
  u_long time_ins  = clockdiff(t3,t2);
  u_long time_read = clockdiff(t4,t3);
  u_long time_tot  = clockdiff(t4,t0);

  cout << "Hashing:     " << w1 << time_hash << " microseconds, " << w2 << (float)totbytes/time_hash << " MB/s" << endl;
  cout << "Compressing: " << w1 << time_comp << " microseconds, " << w2 << (float)totbytes/time_comp << " MB/s" << endl;
  cout << "DB insert:   " << w1 << time_ins  << " microseconds, " << w2 << (float)totbytes/time_ins  << " MB/s" << endl;
  cout << "Reading:     " << w1 << time_read << " microseconds, " << w2 << (float)totbytes/time_read << " MB/s" << endl;
  cout << "Total:       " << w1 << time_tot  << " microseconds, " << w2 << (float)totbytes/time_tot  << " MB/s" << endl;

  delete [] testdata;
  exit(0);
}

/*******************************************************************************
 * Main section - process options etc
 ******************************************************************************/

int main(int argc, char** argv) {
  int c;
  while ((c = getopt(argc, (char **)argv, "DB:b:cdf:hkpqrv?")) != -1) {
    switch(c) {
      case 'D': o_debug.open("/dev/tty");  break;
      case 'B': set_blocksize(optarg);     break;
      case 'b': p_bandwidth=atol(optarg);  break;
      case 'c': p_nocompress = true;       break;
      case 'd': p_dump       = true;       break;
      case 'k': p_keep       = true;       break;
      case 'r': p_noreport   = true;       break;
      case 'f': dbase.filename(optarg);    break;
      case 'p': perftest();                break;
      case 'q': o_verbose.close();         break;
      case 'v': showversion();             break;
      case 'h':
      case '?': showusage(); exit(0);
      default : showusage(); exit(10) ;
    }
  }
  cout << "qdda " << progversion << " - The Quick & Dirty Dedupe Analyzer" << endl;
  if(optind<argc || !isatty(fileno(stdin)) ) { // process files if we have files or stdin
    if (p_keep==false)                  dbase.deletedb();
    if (!isatty(fileno(stdin)))         analyze("/dev/stdin");    // analyze from stdin if not console
    for (int i = optind; i < argc; ++i) analyze(argv[i]);         // analyze the rest of the files
  }
  if(!p_noreport) report();
}
