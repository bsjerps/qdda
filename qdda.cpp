/*******************************************************************************
 * Title       : qdda - quick & dirty dedup analyzer
 * Description : Checks files or block devices for duplicate blocks
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+
 * Disclaimer  : See https://www.gnu.org/licenses/gpl-3.0.txt
 * More info   : http://outrun.nl/wiki/qdda
 * -----------------------------------------------------------------------------
 * Revision history:
 * 1.0   - First published version
 * 1.2.1 - changed mhash crc32 to zlib crc32 (get rid of mhash lib dependency)
 *         changed zlib to lz4 compression (much better performance)
 *         now analyze all blocks for compression
 *         added thin ratio and other output changes
 * 1.3.0 - added All flash array aware compression (buckets), variable blocksize
 * 1.4.1 - Various improvements
 * 1.5.1 - Major performance increase when processing large amounts of data
 * 1.6.0 - Import function, file list, purge always after scanning, histograms
 * ---------------------------------------------------------------------------
 * Build notes: Requires lz4 >= 1.7.1
 ******************************************************************************/

#include "qdda.h"
using namespace std;

/*******************************************************************************
 * global parameters - modify at own discretion
 ******************************************************************************/

const char   *progversion   = "1.6.0";
const char   *dbpath        = "/var/tmp/qdda.db"; // default database location
const u_long blockspercycle = 64;                 // read chunk blocks at a time when throttling
const u_long updateinterval = 10000;              // progress report every N blocks
const u_long commitinterval = 20000;              // commit every N blocks (rows)
const int    col1w          = 15;                 // 1st column - max 9TB without messing up
const int    col2w          = 10;                 // 2nd column

/*******************************************************************************
 * Constants - don't touch
 ******************************************************************************/

const long mebibyte      = 1024*1024; // Bytes per MiB
const long max_blocksize = 65536;     // max allowed blocksize

/*******************************************************************************
 * Initialization - globals
 ******************************************************************************/

sqlitedb       dbase;                    // global access to database
std::ofstream  o_debug;                  // Debug stream, open with -D option
std::ofstream  o_verbose("/dev/stdout"); // Progress stream, disable with -q option

// option parameters/switches
bool   p_nocompress = false;  // disable compression analysis
bool   p_noreport   = false;  // disable results report
bool   p_dryrun     = false;  // disable SQL updates
bool   p_keep       = false;  // delete database unless
bool   p_dump       = false;  // dump block offsets and hashes
u_long p_blksz      = 8192;   // default blocksize
u_long p_bandwidth  = 200;    // default bandwidth throttle (MB/s)


/*******************************************************************************
 * Usage
 ******************************************************************************/

const char * version_info  = "build date: " __DATE__  "\nbuild time: " __TIME__ R"(

Copyright (C) 2017 Bart Sjerps <bart@outrun.nl>
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
)";

string usage_text = R"(
Usage: qdda [-D] [-B blksize] [-b <bandw>] [-c] [-d] [-f <dbpath>] [-i importdb] [-k] [-n] [-q] [-r] [-t] [-v] [-x] [file list]
  -D (debug)        : Show lots of annoying debug info
  -B <blksize_kb>   : Set blocksize to blksize_kb kilobytes
  -b <bandw>        : Throttle bandwidth in MB/s (default 200, set to 0 to disable throttling)
  -c (no-Compress)  : Skip compression estimate
  -d (dump)         : Dump block offsets and hashes (to search for block offset with hash)
  -f <database>     : Specify alternative database location (default /var/tmp/qdda.db)
  -i <import-db>    : Import data from another database (merge with existing data)
  -k (keep)         : Keep existing database (don't delete) - to feed additional data with multiple runs
  -n (noupdate)     : Dryrun - just read data (no processing)
  -q (quiet)        : Don't show progress indicator or intermediate results
  -r (no-Report)    : Don't show report
  -t (perftest)     : Run raw performance test with 10GB random data
  -v (version)      : print version and copyright info
  -x (eXtended)     : Extended report (file info and histograms)

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

void showversion() { cout << version_info << endl; exit(0); }
void showusage()   { cout << usage_text << endl; }

/*******************************************************************************
 * SQL query text
 ******************************************************************************/

// Schema definition: // key-value store in SQLite: key = hash of block, v = block count, b=compressed bytes
// kb is staging table: f=filenum, k = key, b=compressed bytes
// metadata keeps track of the blocksize - cannot chance blocksize once created. Force metadata to have one row only.
// files keeps track of which and how many files were imported.

const char * sql_schema = "pragma page_size = 8192;\n"
    "CREATE TABLE IF NOT EXISTS kv(k integer primary key, v integer, b integer);\n"
    "CREATE TABLE IF NOT EXISTS staging(f integer,k integer, b integer);\n"
    "CREATE TABLE IF NOT EXISTS files(id integer primary key autoincrement, name TEXT, blocks integer, size integer);\n"
    "CREATE TABLE IF NOT EXISTS metadata(\n"
    "lock char(1) not null default 1\n"
    ", blksz integer\n"
    ", constraint pk_t1 primary key(lock)\n"
    ", constraint ck_t1_l check (lock=1))\n";

const char *sql_optimize = "PRAGMA journal_mode = OFF;PRAGMA synchronous = OFF"; // performance optimizations 
const char *sql_cache    = "PRAGMA cache_size = 10000";
const char *sql_fileid   = "select coalesce(max(id),0) from files";
const char *sql_blksz    = "select blksz from metadata";
const char *sql_blkcount = "select count(*) from staging";
const char *sql_commit   = "END TRANSACTION;BEGIN TRANSACTION";
const char *sql_insert   = "insert into staging(f,k,b) values (?,?,?)";
const char *sql_purge    = "delete from staging;vacuum;";
const char *sql_import   = "insert or replace into main.kv\n"
                           "select tmpdb.kv.k\n"
                           ", coalesce(main.kv.v,0) + tmpdb.kv.v\n"
                           ", tmpdb.kv.b from tmpdb.kv\n"
                           "left outer join main.kv on main.kv.k = tmpdb.kv.k\n"
                           "group by tmpdb.kv.k\n"
                           "order by main.kv._rowid_,tmpdb.kv.k\n";
const char *sql_merge    = "insert or replace into kv\n"
                           "select staging.k\n"
                           ", coalesce(kv.v,0) + count(staging.k)\n"
                           ", staging.b from staging\n"
                           "left outer join kv on kv.k = staging.k\n"
                           "group by staging.k\n"
                           "order by kv._rowid_,staging.k";

/****************************************************************************
* Various
****************************************************************************/

// true if file exists
int file_exists(string& fn) {
  ifstream f(fn.c_str());
  if(!f.fail()) return 1;
  return 0;
}

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
  if(p_nocompress==true) return size;                           // if compression is disabled, return blocksize
  static char buf[max_blocksize+1024];                          // compressed block may be larger than uncompressed
  int result = LZ4_compress_default(src, buf, size, size+1024); // call LZ4 compression lib, only use bytecount
  if(result>size) return size;                                  // don't compress if size is larger
  if(result==0) die("Compression error");
  return result;
}

// Throttling mechanism - microsleep every <bytes> to adjust read MB/s
void throttle(const u_long& bandwidth,const u_long& bytes) {
  if(!bandwidth) return;                   // immediately return if we don't throttle
  static u_long cycletime = 0;             // cycle time (required microseconds per chunk)
  static auto   prev      = clocknow;      // keep t1 between calls
  auto          now       = clocknow;      // current time
  if(!cycletime) {                         // initialization when first called
    cycletime = bytes / bandwidth;         // every <cycletime> ms, process <bytes> in servicetime and sleep for rest of the period
    o_verbose << "Throttling @ " << bandwidth << " MB/s (cycletime=" << cycletime << " us," << bytes/1024 << " KiB per cycle)" << endl;
    return;
  };
  u_long servicetime = clockdiff(now,prev);
  if(cycletime > servicetime) usleep(cycletime - servicetime);
  prev=clocknow; // measure time diff for next call
}

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

void showdump(u_long block, u_long hash,u_long bytes) {
  cout << dec << setw(9) << setfill('0') << block << ","  
       << hex << setw(8) << hash << ","
       << dec << setw(5) << bytes << endl;
}

// Show progress information, updated every N blocks
void showprogress(int newfnum, u_long blocks, u_long bytes, const char * msg = NULL) {
  static auto   start = clocknow;    // time of start processing file
  static auto   prev  = clocknow;    // time of previous call
  static int    fnum  = 0;           // previous file num (tells us when new file process starts)
  static u_long prevb = 0;           // keep track of previous byte count
  auto          now   = clocknow;    // current time
  if(fnum!=newfnum) { start=now ; fnum=newfnum ; }          // initialize when processing a new file
  auto avgsvctm = clockdiff(now,start);                     // service time of xx bytes since start of file
  auto cursvctm = clockdiff(now,prev);                      // service time of xx bytes since previous call
  auto avgbw = (avgsvctm==0) ? 0 : bytes/avgsvctm;          // bytes per second since start of file
  auto curbw = (cursvctm==0) ? 0 : (bytes-prevb)/cursvctm;  // bytes per second since previous call
  stringstream ss;                                          // generate a string with the progress message
  ss << "File " << setfill('0') << setw(2) << fnum << ", " << blocks << " blocks, " 
     << bytes/mebibyte << " MiB processed, " << curbw << " MB/s, " << avgbw << " MB/s avg";
  if(msg) ss << msg;                                        // add message if specified
  ss << "                 " ;                               // blank rest of line
  o_verbose << ss.str();
  for(u_int i=0;i<ss.str().length();i++) o_verbose << '\b'; // returns the cursor to beginning of line
  o_verbose << flush;
  prev=now; prevb=bytes;                                    // save values for next call
}

// Print stats report
void report() {
  dbase.open(); // open db if it wasn't open already
  os_reset();   // reset std::cout state

  auto     blocksize  = dbase.select_long("select blksz from metadata");                       // Blocksize used when scanning
  blockval blk_total  = dbase.select_long("select sum(v) from kv");                            // Total scanned blocks
  blockval blk_free   = dbase.select_long("select v from kv where k=0");                       // Total zero blocks
  blockval blk_used   = dbase.select_long("select sum(v) from kv where k!=0");                 // Total non-zero blocks
  blockval blk_dedup  = dbase.select_long("select count(*) from kv where k!=0");               // Unique hashes (deduped)
  blockval blk_count1 = dbase.select_long("select count(v) from kv where k!=0 and v=1");       // Hashes with count=1 (non-dedupeable data)
  blockval blk_count2 = dbase.select_long("select count(v) from kv where k!=0 and v=2");       // Hashes with count=2 (dedupable data)
  blockval blk_count3 = dbase.select_long("select count(v) from kv where k!=0 and v=3");       // Hashes with count=3 (dedupable data)
  blockval blk_count4 = dbase.select_long("select count(v) from kv where k!=0 and v=4");       // Hashes with count=4 (dedupable data)
  blockval blk_counth = dbase.select_long("select count(v) from kv where k!=0 and v>4");       // Hashes with count=5 (dedupable data)
  auto     bytes_comp = dbase.select_long("select sum(b)   from kv where k!=0");               // Total bytes after full compression
  auto     buckets_2k = dbase.select_long("select count(*) from kv where b>0    and b<=2048"); // Blocks that compress into 2k
  auto     buckets_4k = dbase.select_long("select count(*) from kv where b>2048 and b<=4096"); // Blocks that compress into 2k+1 - 4k
  auto     buckets_8k = dbase.select_long("select count(*) from kv where b>4096");             // Blocks that don't fit in other buckets

  blk_total.setblocksize(blocksize);

  // calc compression buckets
  u_long blk_2k = buckets_2k * 2048 / blocksize; // 1 8K block holds 4 2K buckets
  u_long blk_4k = buckets_4k * 4096 / blocksize; // 1 8K block holds 2 4K buckets
  u_long blk_8k = buckets_8k * 8192 / blocksize; // blocks that don't fit require entire block
  blockval blk_rq = blk_2k + blk_4k + blk_8k;    // required blocks after bucket compression

  // calc ratios - divide by zero results in value 0
  float comp_ratio   = 1 - safe_div (bytes_comp,blk_dedup.bytes()); // full compression ratio
  float dedup_ratio  = safe_div (blk_used,  blk_dedup);             // dedupe ratio
  float used_ratio   = safe_div (blk_used,  blk_total);             // % used vs free
  float thin_ratio   = safe_div (blk_total, blk_used);              // % free vs used (thin provisioning)
  float bucket_ratio = safe_div (blk_dedup, blk_rq);                // bucket compression ratio (sorted into slots)

  // dump the formatted report
  cout << setprecision(2) << fixed; // display float as xyz.ab
  cout    << "                      " << w1 << "*** Details ***"
  << endl << "blocksize           = " << w1 << to_string(blocksize/1024) + " KiB"
  << endl << "total               = " << w1 << printMIB(blk_total.bytes())  << " (" << w2 << blk_total  << " blocks)"
  << endl << "free                = " << w1 << printMIB(blk_free.bytes() )  << " (" << w2 << blk_free   << " blocks)"
  << endl << "used                = " << w1 << printMIB(blk_used.bytes() )  << " (" << w2 << blk_used   << " blocks)"
  << endl << "unique              = " << w1 << printMIB(blk_count1.bytes()) << " (" << w2 << blk_count1 << " blocks)"
  << endl << "deduped 2x          = " << w1 << printMIB(blk_count2.bytes()) << " (" << w2 << blk_count2 << " blocks)"
  << endl << "deduped 3x          = " << w1 << printMIB(blk_count3.bytes()) << " (" << w2 << blk_count3 << " blocks)"
  << endl << "deduped 4x          = " << w1 << printMIB(blk_count4.bytes()) << " (" << w2 << blk_count4 << " blocks)"
  << endl << "deduped >4x         = " << w1 << printMIB(blk_counth.bytes()) << " (" << w2 << blk_counth << " blocks)"
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

sqlquery::sqlquery(sqlitedb& db, const char * query) {
  if(!db.db) die("Creating query: Database not open");
  const char * pzTest;
  stmt = NULL;
  if(sqlite3_prepare_v2(db.db, query, strlen(query), &stmt, &pzTest)!=SQLITE_OK) die("SQL Prepare error");
}

sqlquery::~sqlquery() { 
  int rc=0;
  if(stmt) rc=sqlite3_finalize(stmt);
  if(rc) cerr << "SQL query finalize error " << rc << ", query= " << sqlite3_sql(stmt) << endl;
}

int sqlquery::next() {
  int rc=sqlite3_step(stmt);
  if(rc==SQLITE_ROW) return 1;
  else sqlite3_reset(stmt);
  return 0;
}

u_long sqlquery::exec(const u_long p1) {
  if(!stmt) die("Query exec statement error");
  u_long retval = 0;
  sqlite3_bind_int64(stmt, 1, p1);
  sqlite3_step(stmt);
  retval = sqlite3_column_int64(stmt, 0);
  sqlite3_reset(stmt);
  return retval;
}

u_long sqlquery::exec(const u_long p1, const u_long p2) {
  if(!stmt) die("Query exec statement error");
  u_long retval = 0;
  sqlite3_bind_int64(stmt, 1, p1);
  sqlite3_bind_int64(stmt, 2, p2);
  sqlite3_step(stmt);
  retval = sqlite3_column_int64(stmt, 0);
  sqlite3_reset(stmt);
  return retval;
}

u_long sqlquery::exec(const u_long p1, const u_long p2,const u_long p3) {
  if(!stmt) die("Query exec statement error");
  u_long retval = 0;
  sqlite3_bind_int64(stmt, 1, p1);
  sqlite3_bind_int64(stmt, 2, p2);
  sqlite3_bind_int64(stmt, 3, p3);
  sqlite3_step(stmt);
  retval = sqlite3_column_int64(stmt, 0);
  sqlite3_reset(stmt);
  return retval;
}

void sqlitedb::close() { if(db) { sqlite3_close(db); db = 0; }}

// Open database and create tables if needed
int sqlitedb::open() {
  int rc = 0;
  if(db) return 0;
  rc = sqlite3_open(fn.c_str(), &db);
  if(rc) {
    stringstream errmsg;
    errmsg << "Can't open database, filename: " << fn << ": " << (char *)sqlite3_errmsg(db);
    die (errmsg.str());
  } else {
    sql("pragma schema_version"); // trigger error if not open
    sql(sql_cache);
    sql(sql_schema);
    sql(sql_optimize);
    auto blksz = select_long(sql_blksz);
    if(!blksz) sql("insert into metadata (blksz) values (" + to_string(p_blksz) + ")");
    o_debug << "DB opened: " << fn << endl;
  }
  return(rc);
}

// test if specified file is SQLite3 file
int file_sqlite3(string& fn) {
  const char * magic = "SQLite format 3";
  char buf[32];
  ifstream f(fn.c_str());
  if (!f.good()) return 0;           // can't open file - nonexistent?
  f.seekg (0, f.beg);                // start at begin of file
  f.read (buf,strlen(magic));        // read the magic string
  buf[strlen(magic)]=0;              // force terminate with \0
  if(strcmp(magic,buf)==0) return 1; // compare with magic
  return 0;
}

// delete file only if it exists and is an SQLite 3 database
int file_delete(string& fn) {
  int rc = 0;
  if(!file_exists(fn))  return 1;
  if(!file_sqlite3(fn)) return 1;
  o_debug << "Deleting file " << fn << endl;
  rc = unlink(fn.c_str());
  if (rc==1) { die ("Deleting database failed"); }
  return 0;
}

/* non-optimized SQL, don't use inside long loops */

// Run SQL statement, ignore results (update/insert)
void sqlitedb::sql(const string& query) {
  auto t_start = clocknow;
  o_debug << "query=" << query << flush;
  int rc=0;
  char * errmsg;
  rc = sqlite3_exec(db, query.c_str(), 0, 0, &errmsg);
  if( rc != SQLITE_OK ) {
    cerr << "SQL error: " << errmsg << ", SQL: " << query << endl;
    die("Cannot execute SQL");
  }
  auto t_diff = clockdiff(clocknow,t_start);
  o_debug << ", runtime " << t_diff << " microsec (" << to_string((float)t_diff/1000000,2) << " s)" << endl;
}

void save_filemeta(const string& name, u_long blocks, u_long bytes) {
  stringstream fq;
  fq << "insert into files(name,blocks,size) values ('" << name << "'," 
     << blocks << "," << bytes << ")";
  dbase.sql(fq.str());
}

// SQLite3 callback function - fills struct qresult with header and query result
int sqlitedb::callback(void *r, int argc, char **argv, char **azColName) {
   qresult *qr = (qresult *)r;
   stringstream h,d;
   if(qr->header.empty()) {
     for(int i=0; i<argc; i++) {
       if(qr->tabs[i])h << setw(abs(qr->tabs[i]));
       if(qr->tabs[i]<0) h << right; else h << left;
       h << azColName[i] << " ";
     }
     qr->header = h.str();
   }
   if(!qr->data.empty()) qr->data += "\n";
   for(int i=0; i<argc; i++) {
     if(qr->tabs[i]) d << setw(abs(qr->tabs[i]));
     if(qr->tabs[i]<0) d << right; else d << left;
     d <<(const char *)(argv[i]?argv[i]:"NULL") << " ";
   }
   qr->data += d.str();
   return 0;
}

// Run query and show header and rows
int sqlitedb::runquery(const char * tabstr,const string& query) {
  const int maxtabs = 32;
  qresult r;                                                // result struct to pass to callback
  int tabs[maxtabs];for (int i=0;i<maxtabs;i++) tabs[i]=0;  // array of tab stops
  char buf[maxtabs*8];
  const char * token;
  strcpy(buf,tabstr);
  for(int i=0;i<maxtabs;i++) {                              // split tabstring into array of ints
    token=strtok(i?NULL:buf,",");
    if(!token) break;
    tabs[i]=atoi(token);
  }
  r.tabs = tabs;                                            // add tabs* to results for callback
  char *zErrMsg = 0;
  int rc = sqlite3_exec(db, query.c_str(), callback, (void*)&r, &zErrMsg);
  cout << r.header << endl << r.data << endl;
  return rc;
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

// process file
void analyze(const char * filename) {
  char buf[max_blocksize]; // buffer for calculating checksums etc
  ifstream f;              // file to be processed
  int fnum            = 0; // file number
  u_int  hash         = 0; // crc32 will be stored here
  u_long blocks       = 0; // block counter
  u_long bytes        = 0; // byte counter
  u_int  cbytes       = 0; // compressed bytes

  f.open(filename);
  if (!f.is_open()) die("File open failed");

  dbase.open();                                          // does nothing if already open
  fnum = dbase.select_long(sql_fileid) + 1;              // increase file counter
  sqlquery q_insert(dbase, sql_insert);                  // setup prepared insert query
  auto blksz = dbase.select_long(sql_blksz);             // get blocksize from db
  auto pages = dbase.select_long("pragma cache_size");   // get & show cache size
  o_debug << "Cache pages: " << pages << endl;

  // start looping through file blocks
  dbase.sql("BEGIN TRANSACTION");
  while (!f.eof()) {
    cbytes = 0;                                          // 0 for zero block
    memset(buf,0,blksz);                                 // clear buffer
    f.read(buf,blksz);                                   // read 1 block
    if(f.gcount()==0) break;                             // zero bytes read = end reached
    bytes += f.gcount();
    if(!p_dryrun) {
      hash = blockhash(buf,blksz);                       // get the crc32 hash, or 0 if zero block
      if(hash)      cbytes = compress(buf,blksz);        // get compressed size
      if(p_dump)    showdump(blocks,hash,cbytes);        // dump hashes with -d option
      if(!p_dryrun) q_insert.exec(fnum,hash,cbytes);     // insert or update into database temp table
    }
    blocks++;
    if(blocks%commitinterval==0) dbase.sql(sql_commit);                       // commit every n rows
    if(blocks%updateinterval==0) showprogress(fnum,blocks,bytes);             // progress indicator
    if(blocks%blockspercycle==0) throttle(p_bandwidth, blksz*blockspercycle); // throttle bandwidth
  };
  dbase.sql("END TRANSACTION");
  save_filemeta(filename,blocks,bytes);
  // Merge staging data into kv table, track & display time to merge
  auto t_begin = clocknow;
  showprogress(fnum,blocks,bytes," Importing...");
  dbase.sql(sql_merge);
  auto time_merge = clockdiff(clocknow,t_begin);
  dbase.sql(sql_purge);
  string ss = ", Imported in ";
  ss += to_string((float)time_merge/1000000,2) + " sec";
  showprogress(fnum,blocks,bytes,ss.c_str());
  o_verbose << endl;
};

// import data from another qdda database
void import(const char *fn) {
  dbase.open();
  stringstream q_attach;
  q_attach << "attach database '" << fn << "' as tmpdb";
  dbase.sql(q_attach.str());
  auto blksz1  = dbase.select_long("select blksz from metadata");
  auto blksz2  = dbase.select_long("select blksz from tmpdb.metadata");
  auto blocks1 = dbase.select_long("select count(*) from kv");
  auto blocks2 = dbase.select_long("select count(*) from tmpdb.kv");
  if(blksz1 != blksz2) die("Incompatible blocksize");
  cout << "Adding " << blocks2 << " blocks from " << fn << " to " << blocks1 << " existing blocks" << endl;
  dbase.sql(sql_import);
  dbase.sql("insert into files(name,blocks,size) select name,blocks,size from tmpdb.files");
}

// refuse to set blocksize too high
void set_blocksize(const char * in) {
  long bsize = atol(in);
  if(bsize*1024>max_blocksize) die("Blocksize exceeded");
  p_blksz=1024*bsize;
}

void perftest(const long gb = 10) {
  const long blocksize = 8192;
  const long totbytes  = gb * 1024 * mebibyte;
  const long bufsize   = 16 * mebibyte;
  const long cycles    = totbytes / bufsize;
  const long blocks    = bufsize/blocksize;
  char      *testdata  = new char[bufsize];
  auto       t_begin   = clocknow;

  memset(testdata,0,bufsize);                             // clear buffer
  for(long i=0;i<bufsize;i++) testdata[i] = rand() % 256; // fill test buffer with random data
  dbase.open();

  cout << fixed << setprecision(2);
  cout << "Test set:    " << w1 << cycles * blocks << " random 8k blocks (" << totbytes/mebibyte << " MiB)" << endl;

  // test hashing performance
  t_begin = clocknow;
  for(long j=0;j<cycles;j++) for(long i=0;i<blocks;i++) blockhash(testdata + i*blocksize,blocksize);
  auto time_hash = clockdiff(clocknow,t_begin);

  // test compression performance
  t_begin = clocknow;
  for(long j=0;j<cycles;j++) for(long i=0;i<blocks;i++) compress(testdata + i*blocksize,blocksize);
  auto time_compress = clockdiff(clocknow,t_begin);

  // test sqlite insert performance
  t_begin = clocknow;
  sqlquery q_insert(dbase, sql_insert);
  dbase.sql("BEGIN TRANSACTION");
  for(long j=0;j<cycles;j++) for(long i=0;i<blocks;i++) q_insert.exec(0,i,8192); 
  dbase.sql("END TRANSACTION");
  auto time_insert = clockdiff(clocknow,t_begin);

  auto time_total = time_hash + time_compress + time_insert;
  cout << "Hashing:     " << w1 << time_hash     << " microseconds, " << w2 << (float)totbytes/time_hash     << " MB/s" << endl;
  cout << "Compressing: " << w1 << time_compress << " microseconds, " << w2 << (float)totbytes/time_compress << " MB/s" << endl;
  cout << "DB insert:   " << w1 << time_insert   << " microseconds, " << w2 << (float)totbytes/time_insert   << " MB/s" << endl;
  cout << "Total:       " << w1 << time_total    << " microseconds, " << w2 << (float)totbytes/time_total    << " MB/s" << endl;

  dbase.sql(sql_purge);
}

void xreport() {
  dbase.open();
  cout << endl << "File list:" << endl;
  dbase.runquery("5,12,8,11","select id as File, blocks as Blocks, size/1024/1024 as MiB, name as Filename from files");
  cout << endl << "Dedupe histogram:" << endl;
  dbase.runquery("12,12","select v as 'Dupcount',count(v) as Blocks from kv where k!=0 group by 1 order by v");
  cout << endl << "Compression Histogram:" << endl;
  dbase.runquery("12,12","select ((b-1)/512)*512+512 as 'Size (KB)',count(*) as Blocks from kv where k!=0 group by 1");
}

/*******************************************************************************
 * Main section - process options etc
 ******************************************************************************/

int main(int argc, char** argv) {
  cout << "qdda " << progversion << " - The Quick & Dirty Dedupe Analyzer" << endl;
  int c;
  while ((c = getopt(argc, (char **)argv, "DB:b:cdf:hi:knqrtvx?")) != -1) {
    switch(c) {
      case 'D': o_debug.open("/dev/tty");    break;
      case 'B': set_blocksize(optarg);       break;
      case 'b': p_bandwidth=atol(optarg);    break;
      case 'c': p_nocompress = true;         break;
      case 'd': p_dump       = true;         break;
      case 'f': dbase.filename(optarg);      break;
      case 'i': import(optarg); exit(0);     break;
      case 'k': p_keep       = true;         break;
      case 'n': p_dryrun     = true;         break;
      case 'q': o_verbose.close();           break;
      case 'r': p_noreport   = true;         break;
      case 't': perftest();     exit(0);     break;
      case 'v': showversion();               break;
      case 'x': xreport();      exit(0);     break;
      case 'h':
      case '?': showusage(); exit(0);
      default : showusage(); exit(10) ;
    }
  }
  if(optind<argc || !isatty(fileno(stdin)) ) {                 // process files if we have files or stdin
    if (p_keep==false) { file_delete(dbase.filename()); }      // delete database file at start
    if (!isatty(fileno(stdin)))         analyze("/dev/stdin"); // analyze from stdin if not console
    for (int i = optind; i < argc; ++i) analyze(argv[i]);      // analyze the rest of the files
    if(!p_noreport) report();
  } else {
    if(!p_noreport) report();                                  // No input - only print report
  }
}
