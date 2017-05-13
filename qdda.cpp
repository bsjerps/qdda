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
 * 1.7.0 - Change to MD5, performance improvements, tmpdir, improved reports
 * 1.7.1 - Minor bugfixes, improved long report, improved progress indicator,
 *         -a (append) option replaces -k (keep)
 * ---------------------------------------------------------------------------
 * Build notes: Requires lz4 >= 1.7.1
 ******************************************************************************/

#include "qdda.h"
using namespace std;

/*******************************************************************************
 * global parameters - modify at own discretion
 ******************************************************************************/

const char  *progversion   = "1.7.1";
const char  *dbpath        = "/var/tmp/qdda.db"; // default database location
const char  *tmppath       = "/var/tmp";         // tmpdir for SQLite temp files
const ulong blockspercycle = 64;                 // read chunk blocks at a time when throttling
const ulong updateinterval = 10000;              // progress report every N blocks
const ulong commitinterval = 20000;              // commit every N blocks (rows)
const int   col1w          = 15;                 // 1st column - max 9TB without messing up
const int   col2w          = 10;                 // 2nd column
const int   bucketsize     = 2048;               // Minimum bucketsize

/*******************************************************************************
 * Constants - don't touch
 ******************************************************************************/

const ulong mebibyte      = 1048576; // Bytes per MiB
const ulong max_blocksize = 65536;   // max allowed blocksize
const int   bucketrange   = bindepth(max_blocksize/bucketsize); // array size for buckets

/*******************************************************************************
 * Initialization - globals
 ******************************************************************************/

sqlitedb       dbase;                    // global access to database
std::ofstream  o_debug;                  // Debug stream, open with -D option
std::ofstream  o_verbose("/dev/stdout"); // Progress stream, disable with -q option

// option parameters/switches
bool   p_nocomp     = false;  // disable compression analysis
bool   p_norep      = false;  // disable results report
bool   p_dryrun     = false;  // disable SQL updates
bool   p_append     = false;  // don't delete database, just append
bool   p_dump       = false;  // dump block offsets and hashes
bool   p_import     = false;  // run import
bool   p_vacuum     = false;  // run vacuum
bool   p_test       = false;  // run perftest
bool   p_xrep       = false;  // run extended report
bool   p_version    = false;  // run version info
ulong  p_blksz      = 8192;   // default blocksize
ulong  p_bandwidth  = 200;    // default bandwidth throttle (MB/s)

// class blockval stuff
ulong blockval::blocksz = 0; // static initialization outside class

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
Usage: qdda [-D] [-B blksize] [-b <bandw>] [-c] [-d] [-f <dbpath>] [-i importdb] [-k] [-n] [-q] [-r] [-t [gb]] [-v] [-x] [file list]
  -D (debug)        : Show lots of annoying debug info
  -B <blksize_kb>   : Set blocksize to blksize_kb kilobytes
  -P (purge)        : Reclaim unused space in database (sqlite vacuum)
  -T <tempdir>      : Use tempdir instead of /var/tmp for SQLite temp files (or use export SQLITE_TMPDIR)
  -a (append)       : Append data to existing database
  -b <bandw>        : Throttle bandwidth in MB/s (default 200, set to 0 to disable throttling)
  -c (no-Compress)  : Skip compression estimate
  -d (dump)         : Dump block offsets and hashes (to search for block offset with hash)
  -f <database>     : Specify alternative database location (default /var/tmp/qdda.db)
  -i <import-db>    : Import data from another database (merge with existing data)
  -n (noupdate)     : Dryrun - just read data (no processing)
  -q (quiet)        : Don't show progress indicator or intermediate results
  -r (no-Report)    : Don't show report / don't merge
  -t<size_gb>       : Run raw performance test (no opts) or merge test (data size in GB)
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
dupcount = 2        = Blocks that appear exactly 2 times
dupcount > 2        = Blocks that appear more than 2 times (more detail: run histograms)
deduped             = Required capacity after deduplication
buckets 2k          = Compressed blocks that fit in 2k slots (4 per 8K block)
buckets 4k          = Compressed blocks that fit in 4k slots (2 per 8K block)
buckets 8k          = Compressed blocks that fit in 8k slots (1 per 8K block)
compressed (full)   = Sum of compressed block bytes (compressed with LZ4)
compressed (bucket) = Sum of bucket sizes (compressed and sorted in buckets)
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

// SQLite Schema definition: 
// kv: key-value store, k = hash of block, v = block count, b = compressed bytes
// staging table: k = key, b=compressed bytes
// metadata keeps track of the blocksize - cannot chance blocksize once created. Force metadata to have one row only.
// files keeps track of which and how many files were imported.

const char * sql_schema = R"(pragma page_size = 8192;
CREATE TABLE IF NOT EXISTS metadata(lock char(1) not null default 1, blksz integer, constraint pk_t1 primary key(lock), constraint ck_t1_l check (lock=1));
CREATE TABLE IF NOT EXISTS files(id integer primary key autoincrement, name TEXT, blocks integer, size integer);
CREATE TABLE IF NOT EXISTS kv(k unsigned integer primary key, v integer, b integer);
CREATE TABLE IF NOT EXISTS staging(k integer, b integer);
)";

const char *sql_optimize = "PRAGMA journal_mode = off;PRAGMA synchronous = OFF";
const char *sql_cache    = "PRAGMA cache_size = 10000";
const char *sql_fileid   = "select coalesce(max(id),0) from files";
const char *sql_blksz    = "select blksz from metadata";
const char *sql_blkcount = "select count(*) from staging";
const char *sql_commit   = "END TRANSACTION;BEGIN TRANSACTION";
const char *sql_insert   = "insert into staging(k,b) values (?,?)";
const char *sql_purge    = "drop index if exists staging_ix;delete from staging;";
const char *sql_index    = "create index if not exists staging_ix on staging(k,b)";
const char *sql_import   = R"(insert or replace into main.kv
select tmpdb.kv.k
, coalesce(main.kv.v,0) + tmpdb.kv.v
, tmpdb.kv.b from tmpdb.kv
left outer join main.kv on main.kv.k = tmpdb.kv.k
group by tmpdb.kv.k
order by main.kv._rowid_,tmpdb.kv.k
)";
const char * sql_merge   = R"(insert or replace into kv
select staging.k, coalesce(kv.v,0)+count(staging.k), staging.b from staging
left outer join kv on kv.k = staging.k
group by staging.k
)";

/*******************************************************************************
 * Various
 ******************************************************************************/

// test if file exists
int file_exists(string& fn) {
  ifstream f(fn.c_str());
  if(!f.fail()) return 1;
  return 0;
}

/*******************************************************************************
 * Notes on hash algorithm
 * SQLite integer (also used for primary key on kv table) has max 8 bytes and is
 * signed integer. MAX value is 9 223 372 036 854 775 808. The max value of
 * unsigned long (64 bit) is 18446744073709551615UL which result in negative
 * hash values in the kv table. MD5 sum is 128 bit which would not fit in SQLite
 * integers and would be converted to another datatype (TXT or blob).
 * I found hash collisions with CRC32 on large datasets so needed another hash
 * function - see https://en.wikipedia.org/wiki/Birthday_problem#Probability_table
 * which shows 50% chance of collisions with 77000 rows on 32bit hash which equals
 * 630MiB using 8K blocksize. Problem gets worse when increasing the dataset.
 * 6 bytes (48 bits) is a tradeoff between DB space consumed, performance and
 * accuracy and has a 50% chance of collision with 20M rows (164GiB @ 8K)
 * which is fine for datasets up to many terabytes.
 * Rebuild with 56-bit hash will get roughly 1 collision in about 2.4 TiB @ 8K
 * A 64-bit hash would get roughly 1 collision every 41 TiB @ 8K.
 */

// returns the least significant 6 bytes of the md5 hash (16 bytes) as unsigned long
ulong hash6_md5(const char * buf, const int size) {
  unsigned char digest[16];
  const unsigned char * ubuf = (unsigned char *)buf;
  static int f = 0;
  static char zerobuf[max_blocksize];                             // allocate static buffer (once)
  if(!f) { memset(zerobuf,0,max_blocksize); f=1; }                // force hash=0 for zero block
  if(memcmp (buf,zerobuf,size)==0) return 0;                      // return 0 for zero block
  MD5(ubuf,size,digest);                                          // get MD5 (as string)
  return                            // ignore chars 0-9
  /*((ulong)digest[9]  << 48) +*/   // enable this for 56-bit hashes vs 48-bit
    ((ulong)digest[10] << 40) +     // convert char* to ulong but keeping
    ((ulong)digest[11] << 32) +     // the right order, only use lower 6 bytes (char 10-15)
    ((ulong)digest[12] << 24) +     // SQLite integer is 8 byte signed so we need to stay within
    ((ulong)digest[13] << 16) +     // 8 bytes and unsigned. 6 bytes is best compromise
    ((ulong)digest[14] << 8 ) +
    ((ulong)digest[15]);
}

// Get compressed bytes for a compressed block - lz4
u_int compress(const char * src,const int size) {
  if(p_nocomp==true) return size;                               // if compression is disabled, return blocksize
  static char buf[max_blocksize+1024];                          // compressed block may be larger than uncompressed
  int result = LZ4_compress_default(src, buf, size, size+1024); // call LZ4 compression lib, only use bytecount
  if(result>size) return size;                                  // don't compress if size is larger
  if(result==0) die("Compression error");
  return result;
}

void throttle(const ulong cycletime) {
  if(!cycletime) return;                     // immediately return if we don't throttle
  static auto   clockprev   = clocknow;      // keep t1 between calls
  ulong         servicetime = clockdiff(clocknow,clockprev);
  if(cycletime > servicetime) usleep(cycletime - servicetime);
  clockprev=clocknow; // measure time diff for next call
}

/*******************************************************************************
 * Formatting & printing
 ******************************************************************************/

std::string printPRC(float perc) { 
  stringstream ss;
  ss << setfill(' ') << setprecision(4) << perc << " %"; 
  return ss.str(); 
}

std::string printMIB(ulong bytes)   {
  stringstream ss;
  ss << setfill(' ') << fixed << setprecision(2) << setw(4) << (float)bytes / mebibyte << " MiB";
  return ss.str(); 
}
std::string printBLK(ulong blocks)   {
  stringstream ss;
  ss << setfill(' ') << to_string(blocks) + " blocks";
  return ss.str(); 
}

// reset manipulators of an ostream (copy default flags from null)
void os_reset(std::ostream& os = cout) { os.copyfmt(std::ios(NULL)); }

void showdump(ulong block, ulong hash,ulong bytes) {
  cout << dec << setw(9) << setfill('0') << block
       << ","  << hex << setw(18) << showbase << internal << hash 
       << ","  << dec << setw(5)  << bytes << endl;
}

void showprogress(const std::string& str) {
  static unsigned int l = 0;
  l = str.length() > l ? str.length() : l;               // track largest string length
  if(str.length() == 0) {                                // clear line if string empty
    for(u_int i=0;i<l;i++) o_verbose << ' ';
    for(u_int i=0;i<l;i++) o_verbose << '\b';
  } else {
    o_verbose << str;
    for(u_int i=0;i<str.length();i++) o_verbose << '\b'; // returns the cursor to original offset
    o_verbose << flush;
  }
}

// Show progress information, updated every N blocks. 
void progress(ulong blocks, ulong bytes = 0, const char * msg = NULL) {
  static auto  start = clocknow;    // time of start processing file
  static auto  prev  = clocknow;    // time of previous call
  auto         now   = clocknow;    // current time
  static int   fnum  = 0;           // previous file num (tells us when new file process starts)
  static ulong prevb = 0;           // keep track of previous byte count
  if(blocks==0) {                   // reset stats before processing new file
    start=clocknow;
    prev=clocknow;
    prevb=0;
    fnum++;
  }
  auto avgsvctm = clockdiff(now,start);                     // service time of xx bytes since start of file
  auto cursvctm = clockdiff(now,prev);                      // service time of xx bytes since previous call
  auto avgbw = (avgsvctm==0) ? 0 : bytes/avgsvctm;          // bytes per second since start of file
  auto curbw = (cursvctm==0) ? 0 : (bytes-prevb)/cursvctm;  // bytes per second since previous call
  stringstream ss;                                          // generate a string with the progress message
  ss << "File " << setfill('0') << setw(2) << fnum << ", "
     << blocks << " blocks ("  << bytes/mebibyte << " MiB) processed, "
     << curbw << "/" << (p_bandwidth?p_bandwidth:9999) << " MB/s, " << avgbw << " MB/s avg";
  if(msg) ss << msg;                                        // add message if specified
  ss << "                 " ;                               // blank rest of line
  showprogress(ss.str());
  prev=now; prevb=bytes;                                    // save values for next call
}

/*******************************************************************************
 * SQLite stuff
 ******************************************************************************/

void vacuum() {
  dbase.open();
  dbase.sql("vacuum");
}

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

// Return first value of query - one parameter
ulong sqlquery::exec(const ulong p1) {
  if(!stmt) die("Query exec statement error");
  ulong retval = 0;
  sqlite3_bind_int64(stmt, 1, p1);
  sqlite3_step(stmt);
  retval = sqlite3_column_int64(stmt, 0);
  sqlite3_reset(stmt);
  return retval;
}

// Return first value of query - two parameters
ulong sqlquery::exec(const ulong p1, const ulong p2) {
  if(!stmt) die("Query exec statement error");
  ulong retval = 0;
  sqlite3_bind_int64(stmt, 1, p1);
  sqlite3_bind_int64(stmt, 2, p2);
  sqlite3_step(stmt);
  retval = sqlite3_column_int64(stmt, 0);
  sqlite3_reset(stmt);
  return retval;
}

// Return first value of query - three parameters
ulong sqlquery::exec(const ulong p1, const ulong p2,const ulong p3) {
  if(!stmt) die("Query exec statement error");
  ulong retval = 0;
  sqlite3_bind_int64(stmt, 1, p1);
  sqlite3_bind_int64(stmt, 2, p2);
  sqlite3_bind_int64(stmt, 3, p3);
  sqlite3_step(stmt);
  retval = sqlite3_column_int64(stmt, 0);
  sqlite3_reset(stmt);
  return retval;
}

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
    sql("PRAGMA temp_store_directory = '" + tmpdir + "'");
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

// Save file info in DB
void save_filemeta(const string& name, ulong blocks, ulong bytes) {
  stringstream fq;
  fq << "insert into files(name,blocks,size) values ('" << name << "'," << blocks << "," << bytes << ")";
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
     if(qr->tabs[i])   d << setw(abs(qr->tabs[i]));
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
ulong sqlitedb::select_long(const string& query) {
  auto         t_start = clocknow;
  int          rc      = 0;
  ulong        retval  = 0;
  sqlite3_stmt *stmt   = 0;
  const char   *pzTest = NULL;

  o_debug << "query=" << query << flush;
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
  auto t_diff = clockdiff(clocknow,t_start);
  o_debug << ", runtime " << t_diff << " microsec (" << to_string((float)t_diff/1000000,2) << " s)" << endl;
  return retval;
}

/*******************************************************************************
 * Functions
 ******************************************************************************/

// Merge staging data into kv table, track & display time to merge
void merge() {
  stringstream ss1,ss2;
  auto blksz  = dbase.select_long(sql_blksz);
  auto rowskv = dbase.select_long("select count(*) from kv");
  auto rowsst = dbase.select_long("select count(*) from staging");
  auto mibst  = rowsst*blksz/mebibyte;
  auto mibkv  = rowskv*blksz/mebibyte;
  if(!rowsst) return;
  o_verbose << "Adding " << rowsst << " blocks (" << mibst << " MiB) with " << rowskv << " blocks (" << mibkv << " MiB)" << endl;
  ss1 << "Indexing" << flush;
  showprogress(ss1.str());
  auto t_begin = clocknow;
  dbase.sql(sql_index);
  auto time_index  = clockdiff(clocknow,t_begin);
  float index_sec  = (float)time_index/1000000;
  ulong index_rps  = rowsst*1000000/time_index;
  ulong index_mbps = mibst*1000000/time_index;
  ss1 << " in " << setprecision(2) << fixed << index_sec << " sec (" << index_rps << " blocks/s, " << index_mbps << " MiB/s)";
  ss2 << ss1.str() << ", Merging" << flush;
  showprogress(ss2.str());
  t_begin = clocknow;
  dbase.sql(sql_merge);
  auto time_merge = clockdiff(clocknow,t_begin);
  float merge_sec  = (float)time_merge/1000000;
  ulong merge_rps  = rowsst*1000000/time_merge;
  ulong merge_mbps = mibst*1000000/time_merge;
  ss2 << " in " << setprecision(2) << fixed << merge_sec << " sec (" << merge_rps << " blocks/s, " << merge_mbps << " MiB/s)";
  o_verbose << ss2.str() << endl;
  dbase.sql(sql_purge);
}

// process file
void analyze(const char * filename) {
  dbase.open();
  auto blksz = dbase.select_long(sql_blksz); // get blocksize from db
  char buf[max_blocksize];                   // buffer for calculating checksums etc
  ifstream f;                                // file to be processed
  ulong cycletime = 0;                       // throttle cycletime in usec
  ulong hash      = 0;                       // crc32 will be stored here
  ulong blocks    = 0;                       // block counter
  ulong bytes     = 0;                       // byte counter
  uint  cbytes    = 0;                       // compressed bytes
  sqlquery q_insert(dbase, sql_insert);      // setup prepared insert query
  cycletime = safediv_ulong(blksz*blockspercycle,p_bandwidth);

  f.open(filename);
  if (!f.is_open()) die("File open failed");

  // start looping through file blocks
  progress(0);                                           // reset progress counter
  dbase.sql("BEGIN TRANSACTION");
  while (!f.eof()) {
    cbytes = 0;                                          // 0 for zero block
    memset(buf,0,blksz);                                 // clear buffer
    f.read(buf,blksz);                                   // read 1 block
    if(f.gcount()==0) break;                             // zero bytes read = end reached
    bytes += f.gcount();
    hash = hash6_md5(buf,blksz);                         // get the md5 hash (lower 6 bytes), or 0 if zero block
    if(hash)      cbytes = compress(buf,blksz);          // get compressed size
    if(p_dump)    showdump(blocks,hash,cbytes);          // dump hashes with -d option
    if(!p_dryrun) q_insert.exec(hash,cbytes);            // insert or update into database temp table
    blocks++;
    if(blocks%commitinterval==0) dbase.sql(sql_commit);  // commit every n rows
    if(blocks%updateinterval==0) progress(blocks,bytes); // progress indicator
    if(blocks%blockspercycle==0) throttle(cycletime);    // throttle bandwidth
  };
  dbase.sql("END TRANSACTION");
  progress(blocks,bytes);        
  save_filemeta(filename,blocks,bytes);
  o_verbose << endl;
}

// import data from another qdda database
void import(const char * fn) {
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
  ulong bsize = atol(in);
  if(bsize*1024>max_blocksize) die("Blocksize exceeded");
  p_blksz=1024*bsize;
}

// test merging performance with random data
void mergetest(ulong gb) {
  const ulong rowspergb  = 131072;
  const ulong rows       = rowspergb * gb;
  dbase.open();
  sqlquery q_insert(dbase, sql_insert);
  srand (time(NULL));
  cout << "Merge test: Loading " << rows << " blocks (" << rows*8192/mebibyte << " MiB)" << flush;
  for(ulong i=0;i<rows;i++) q_insert.exec(lrand() & 0x0000FFFFFFFFFFFF,rand() % 8192);
  save_filemeta("dummy", rows, rows*8192);
  cout << endl;
  merge();
}

// test hashing, compression and insert performance
void speedtest() {
  const ulong blocksize  = 8192;
  const ulong rowspergb  = 131072;
  const ulong bufsize    = 1024 * mebibyte;
  char       *testdata   = new char[bufsize];
  auto        clockstart = clocknow;

  srand(1);
  memset(testdata,0,bufsize);                              // clear buffer
  for(ulong i=0;i<bufsize;i++) testdata[i] = (char)rand() % 256; // fill test buffer with random data
  dbase.open();

  cout << fixed << setprecision(2);
  cout << "Test set:    " << w1 << rowspergb << " blocks, 8k (" << bufsize/mebibyte << " MiB)" << endl;

  // test hashing performance
  clockstart = clocknow;
  for(ulong i=0;i<rowspergb;i++) hash6_md5(testdata + i*blocksize,blocksize);
  auto time_hash = clockdiff(clocknow,clockstart);

  // test compression performance
  clockstart = clocknow;
  for(ulong i=0;i<rowspergb;i++) compress(testdata + i*blocksize,blocksize);
  auto time_compress = clockdiff(clocknow,clockstart);
  
  // test sqlite insert performance
  clockstart = clocknow;
  sqlquery q_insert(dbase, sql_insert);
  dbase.sql("BEGIN TRANSACTION");
  for(ulong i=0;i<rowspergb;i++) q_insert.exec(i,8192);
  dbase.sql("END TRANSACTION");
  auto time_insert = clockdiff(clocknow,clockstart);

  auto time_total = time_hash + time_compress + time_insert;
  cout << "Hashing:     " << w1 << time_hash     << " usec, " << w2 << (float)bufsize/time_hash     << " MB/s" << endl;
  cout << "Compressing: " << w1 << time_compress << " usec, " << w2 << (float)bufsize/time_compress << " MB/s" << endl;
  cout << "DB insert:   " << w1 << time_insert   << " usec, " << w2 << (float)bufsize/time_insert   << " MB/s" << endl;
  cout << "Total:       " << w1 << time_total    << " usec, " << w2 << (float)bufsize/time_total    << " MB/s" << endl;

  dbase.sql(sql_purge);
  delete[] testdata;
}

void perftest(const char *arg) {
  if (p_append==false) { file_delete(dbase.filename()); }
  if(arg) mergetest(atol(arg));
  else speedtest();
}

/*******************************************************************************
 * Reports
 ******************************************************************************/

// Print stats report
void report() {
  dbase.open();                // open db if it wasn't open already
  os_reset();                  // reset std::cout state
  merge();                     // merge staging data if any
  ulong buckets[bucketrange];  // compression buckets
  ulong blocks[bucketrange];   // compression blocks (each block holds x buckets where x=blocksize/bucketsize)
  sqlquery q_buckets(dbase, "select count(*) from kv where b between ? and ?");
  auto     blocksize   = dbase.select_long("select blksz from metadata");                         // Blocksize used when scanning
  blockval blk_total   = dbase.select_long("select sum(v) from kv");                              // Total scanned blocks
  blockval blk_free    = dbase.select_long("select v from kv where k=0");                         // Total zero blocks
  blockval blk_used    = dbase.select_long("select sum(v) from kv where k!=0");                   // Total non-zero blocks
  blockval blk_dedup   = dbase.select_long("select count(*) from kv where k!=0");                 // Unique hashes (deduped)
  blockval blk_count1  = dbase.select_long("select count(v) from kv where k!=0 and v=1");         // Hashes with count=1 (non-dedupable data)
  blockval blk_count2  = dbase.select_long("select count(v) from kv where k!=0 and v=2");         // Hashes with count=2 (dedupable data)
  blockval blk_counth  = dbase.select_long("select count(v) from kv where k!=0 and v>2");         // Hashes with count>2 (dedupable data)
  auto     bytes_comp  = dbase.select_long("select sum(b)   from kv where k!=0");                 // Total bytes after full compression
  blockval::setblocksize(blocksize);
  blockval blk_rq;
  for(int i=0;i<bucketrange;i++) { // calc compression buckets
    int max = (bucketsize << i);
    int min = i?(1024<<i)+1:1;
    buckets[i]=q_buckets.exec(min,max);
    blocks[i]=buckets[i]*max/blocksize;
    blk_rq = blk_rq + blocks[i];
  }
  // calc ratios - divide by zero results in value 0
  float comp_ratio   = 1 - safediv_float (bytes_comp,blk_dedup.bytes()); // full compression ratio
  float dedup_ratio  = safediv_float (blk_used,  blk_dedup);             // dedupe ratio
  float used_ratio   = safediv_float (blk_used,  blk_total);             // % used vs free
  float thin_ratio   = safediv_float (blk_total, blk_used);              // % free vs used (thin provisioning)
  float bucket_ratio = safediv_float (blk_dedup, blk_rq);                // bucket compression ratio (sorted into slots)

  // dump the formatted report
  cout << setprecision(2) << fixed; // display float as xyz.ab
  cout    << "                      " << w1 << "*** Details ***"
  << endl << "blocksize           = " << w1 << to_string(blocksize/1024) + " KiB"
  << endl << "total               = " << w1 << printMIB(blk_total.bytes())  << " (" << w2 << blk_total  << " blocks)"
  << endl << "free                = " << w1 << printMIB(blk_free.bytes() )  << " (" << w2 << blk_free   << " blocks)"
  << endl << "used                = " << w1 << printMIB(blk_used.bytes() )  << " (" << w2 << blk_used   << " blocks)"
  << endl << "unique              = " << w1 << printMIB(blk_count1.bytes()) << " (" << w2 << blk_count1 << " blocks)"
  << endl << "dupcount = 2        = " << w1 << printMIB(blk_count2.bytes()) << " (" << w2 << blk_count2 << " blocks)"
  << endl << "dupcount > 2        = " << w1 << printMIB(blk_counth.bytes()) << " (" << w2 << blk_counth << " blocks)"
  << endl << "deduped             = " << w1 << printMIB(blk_dedup.bytes())  << " (" << w2 << blk_dedup  << " blocks)" << endl;
  for(int i=0;i<bucketrange;i++) if(buckets[i])
    cout  << "buckets " << setw(2) << (2 << i) << "k         = "
          << w1 << printMIB(buckets[i]*(bucketsize << i))
          << " (" << w2 << buckets[i] << " buckets)" << endl;
  cout    << "compressed (full)   = " << w1 << printMIB(bytes_comp)         << " (" << w2 << comp_ratio*100 << " %)"
  << endl << "compressed (bucket) = " << w1 << printMIB(blk_rq.bytes())     << " (" << w2 << blk_rq     << " blocks)"
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

//create table ttt (x integer, y integer);
//sqlite> insert into ttt values (0,1024),(1025,2048),(2049,4096),(4097,8192),(8193,16384);

void extreport() {
  dbase.open();
  auto blksz = dbase.select_long(sql_blksz); // get blocksize from db
  cout << endl;
  dbase.sql("create temp table hist_c (min integer, max integer)"); // create temp table with min/max values for 'group by' query
  sqlquery q_instmp(dbase, "insert into hist_c values(?,?)");
  for(ulong i=0;i<blksz;i+=1024) q_instmp.exec(i+1,i+1024);

  dbase.runquery("5,8,8,11","select id as File, blksz/1024 as Blksz, blocks as Blocks, size/1024/1024 as MiB, name as Filename from files,metadata");
  cout << endl << "Dedupe histogram:" << endl;
  dbase.runquery("12,12","select v as 'Dupcount',count(v) as Blocks,count(v)*(select blksz from metadata) as Bytes from kv where k!=0 group by 1 order by v");
  cout << endl << "Compression Histogram:" << endl;
  dbase.runquery("12,12","select max as 'Size(KB)',count(b) as Blocks,count(b)*max as Bytes from hist_c left outer join kv on kv.b between min and max group by max");
}

/*******************************************************************************
 * Main section - process options etc
 ******************************************************************************/

int main(int argc, char** argv) {
  cout << "qdda " << progversion << " - The Quick & Dirty Dedupe Analyzer" << endl;
  int c; const char * p = NULL;
  while ((c = getopt(argc, (char **)argv, "B:DPT:ab:cdf:hi:nqrt::vx?")) != -1) {
    switch(c) {
      case 'B': set_blocksize     (optarg); break; // Change default blocksize
      case 'D': o_debug.open("/dev/tty");   break; // Open debug stream
      case 'P': p_vacuum  = true;           break; // Purge unused space in DB
      case 'T': dbase.tempdir     (optarg); break; // Set SQLite tempdir
      case 'a': p_append  = true;           break; // Keep existing DB data, append new
      case 'b': p_bandwidth=atol  (optarg); break; // Limit bandwidth
      case 'c': p_nocomp  = true;           break; // Disable compression
      case 'd': p_dump    = true;           break; // Dump hashes
      case 'f': dbase.filename    (optarg); break; // Set DB filename
      case 'i': p_import  = true; p=optarg; break; // Import from another database
      case 'n': p_dryrun  = true;           break; // Don't update DB
      case 'q': o_verbose.close();          break; // Be quiet (no status updates)
      case 'r': p_norep   = true;           break; // Skip report and merge
      case 't': p_test    = true; p=optarg; break; // Performance test
      case 'v': p_version = true;           break; // Show version
      case 'x': p_xrep    = true;           break; // Extended report
      case 'h':                                    // Help
      case '?': showusage(); exit(0);
      default : showusage(); exit(10) ;
    }
  }
  if(p_vacuum)  { vacuum()      ; exit(0); }
  if(p_import)  { import(p)     ; exit(0); }
  if(p_version) { showversion() ; exit(0); }
  if(p_xrep)    { extreport()   ; exit(0); }
  if(p_test)    { perftest(p)   ; exit(0); }
  if(optind<argc || !isatty(fileno(stdin)) ) {                 // process files if we have files or stdin
    if (p_append==false) file_delete(dbase.filename());        // delete database file at start
    if (!isatty(fileno(stdin)))         analyze("/dev/stdin"); // analyze from stdin if not console
    for (int i = optind; i < argc; ++i) analyze(argv[i]);      // analyze the rest of the files
  }
  if(!p_norep) report();
}
