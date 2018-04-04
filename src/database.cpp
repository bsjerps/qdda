/*******************************************************************************
 * Title       : database.cpp
 * Description : Database code for qdda
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+
 * Disclaimer  : See https://www.gnu.org/licenses/gpl-3.0.txt
 * More info   : http://outrun.nl/wiki/qdda
 * -----------------------------------------------------------------------------
 ******************************************************************************/

#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cstring>
#include <chrono>
#include <unistd.h>

#include <errno.h>
#include <err.h>
#include <sys/stat.h>
#include <vector>
#include <list>

#include "tools.h"
#include "database.h"
#include "qdda.h"

extern bool g_debug;
extern bool g_query;
extern bool g_abort;
extern std::ofstream  c_debug;
extern std::ofstream  c_query;
extern const char* PROGVERSION;
extern ulong          starttime;

// std::shorthands

// #define string std::string
typedef std::string string;

std::ostream& cout = std::cout;
std::ostream& endl (std::ostream& os) { return std::endl(os) ; }


/*******************************************************************************
* SQLite Schema definition:
*
* kv: key-value store, k = hash of block, v = block count, b = compressed bytes
* metadata keeps track of the blocksize - cannot chance blocksize once created. 
* Force metadata to have one row only. 
* Files keeps track of which and how many files were imported.
* buckets holds the min/max sizes of each compression bucket, indexed by search key
* bucketdesc holds the search key <> descriptions
*
******************************************************************************/

/*******************************************************************************
 * Generic functions
 ******************************************************************************/

// test if specified file is SQLite3 file
int fileIsSqlite3(const char * fn) {
  const char * magic = "SQLite format 3";
  char buf[32];
  std::ifstream f(fn);
  if (!f.good()) return 0;           // can't open file - nonexistent?
  f.seekg (0, f.beg);                // start at begin of file
  f.read (buf,strlen(magic));        // read the magic string
  int bytes = f.gcount();
  if(!bytes) return 1;
  buf[strlen(magic)]=0;              // force terminate with \0
  if(strcmp(magic,buf)==0) return 1; // compare with magic
  return 0;
}

// delete file only if it exists and is an SQLite 3 database
int fileDeleteSqlite3(const char * fn) {
  if(!fileExists(fn))  return 1;
  if(!fileIsSqlite3(fn)) return 1;
  c_debug << "Deleting file " << fn << std::endl;
  if (unlink(fn)) err(1, "%s", fn);
  return 0;
}

// wrapper for calls with string instead of char*
int fileExists(const string& fn)        { return fileExists(fn.c_str());}
int fileIsSqlite3(const string& fn)     { return fileIsSqlite3(fn.c_str());}
int fileDeleteSqlite3(const string& fn) { return fileDeleteSqlite3(fn.c_str()); }

// safety guards against overwriting existing files or devices by SQLite
string parseFileName(const string& in) {
  string fn;
  char cwd[80];
  if(!getcwd(cwd, 80)) die("Get CWD failed");
  if((signed char)in.c_str()[0] != '/') { fn = cwd ; fn += '/' ;} // prepend cwd if fn is relative
  fn+=in;                                                         // append input filename
  if (!fn.compare(0,4,"/dev")  )            { die("/dev not allowed in filename: " + fn);}
  if (!fn.compare(0,5,"/proc") )            { die("/proc not allowed in filename: " + fn);}
  if (!fn.compare(0,4,"/sys")  )            { die("/sys not allowed in filename: " + fn);}
  if (!fn.find_last_of("/\\"))              { die("root filesystem not allowed: " + fn);}
  if (fn[fn.length()-1] == '/')             { die("Is directory: " + fn);}
  if(fileExists(fn) && !fileIsSqlite3(fn))  { die("Not a SQlite3 file: " + fn);}
  return fn;
}

/*******************************************************************************
 * Query class functions
 ******************************************************************************/

Query::Query(sqlite3* db, const char * q) { prep(db,q); }
Query::~Query()          { finalize(); }
const char* Query::sql() { return sqlite3_sql(pStmt); }

// prepare the statement
int Query::prep(sqlite3 *db, const char * query) {
  ref = 0; // reset parameter count
  if(!db) return 0;
  const char * pzTest;
  int rc=sqlite3_prepare_v2(db, query, strlen(query), &pStmt, &pzTest);
  if(rc==SQLITE_OK) return 0;
  cout << "MySQL prepare, query: " << query << endl;
  die(sqlite3_errmsg(db));
  return rc; // we never get here
}

int Query::bind(const ulong p) { 
  int rc = sqlite3_bind_int64(pStmt, ++ref, p);
  if(rc==SQLITE_OK) return 0;
  cout << "MySQL bind ulong, return code: " << rc << ", query = " << sql() << endl;
  die(sqlite3_errmsg(sqlite3_db_handle(pStmt)));
  return rc; // we never get here
};

int Query::bind(const char* p) {
  int rc = sqlite3_bind_text (pStmt, ++ref, p, strlen(p),SQLITE_STATIC);
  if(rc==SQLITE_OK) return 0;
  cout << "MySQL bind string, return code: " << rc << endl;
  die(sqlite3_errmsg(sqlite3_db_handle(pStmt)));
  return rc; // we never get here
};

int Query::bind(const string& p) { return bind(p.c_str()); };

int Query::step() {
  if(!pStmt) die("Query statement not prepared");
  if(g_query) print(std::cout);
  int rc = sqlite3_step(pStmt);
  if(rc==SQLITE_DONE) return 0;
  if(rc==SQLITE_ROW) return 0;
  cout << "MySQL step, return code: " << rc << endl;
  die(sqlite3_errmsg(sqlite3_db_handle(pStmt)));
  return rc; // we never get here
}

int Query::reset() {
  if(g_query) std::cout << std::endl << std::flush;
  ref=0;
  int rc = sqlite3_reset(pStmt);
  if(rc==SQLITE_OK) return 0;
  cout << "MySQL reset, return code: " << rc << endl;
  die(sqlite3_errmsg(sqlite3_db_handle(pStmt)));
}

//char *sqlite3_expanded_sql(sqlite3_stmt *pStmt);
// simple exec of prepared statement, no return value
void Query::exec() {
  step();
  reset();
}
// simple exec of prepared statement, ulong return value
ulong Query::execl() {
  step();
  ulong retval = sqlite3_column_int64(pStmt, 0);
  if(g_query) std::cout << " = " << retval;
  // handle multiple rows returned (SQLITE_DONE)
  reset();
  return retval;
}

ulong Query::execl(ulong p1) {
  bind(p1);
  return execl();
}
ulong Query::execl(ulong p1, ulong p2) {
  bind(p1);bind(p2);
  return execl();
}


// execute prepared statement, return string
const string Query::exectext() {
  string retval;
  const unsigned char * pText = NULL;
  step();
  pText = sqlite3_column_text(pStmt, 0);
  retval = (char*) pText;
  if(g_query) std::cout << " = " << retval;
  reset();
  return retval;
}

// cleanup
void Query::finalize() {
  sqlite3_finalize(pStmt);
  pStmt = NULL;
}

void Query::report(const string& tabstr) {
  char separator = ' ';
  std::vector<int>  tabs;
  int               rc = 0;  
  string            str;
  std::stringstream ss(tabstr);
  
  while(ss.good()) {
    getline(ss,str,',');
    tabs.push_back(atoi(str.c_str()));
  }
  if(g_query) { print(std::cout); cout << endl; }
  int cols = sqlite3_column_count(pStmt);
  for(int i=0;i<cols; i++) {
    if(tabs[i]>0) cout << std::left;
    else cout << std::right;
    if(i<tabs.size()) cout << std::setw(abs(tabs[i]));
    cout << sqlite3_column_name(pStmt,i);
    if(i<cols-1) cout << separator;
  }
  cout << endl;

  rc=sqlite3_step(pStmt);
  while(rc==SQLITE_ROW) {
    for(int i=0;i<cols;i++) {
      if(tabs[i]>0) cout << std::left;
      else cout << std::right;
      int coltype = sqlite3_column_type(pStmt,i);
      if(i<tabs.size()) cout << std::setw(abs(tabs[i]));
      cout << std::setprecision(2) << std::fixed;
      switch(coltype) {
        case SQLITE_INTEGER: cout << sqlite3_column_int64(pStmt,i) ; break;
        case SQLITE_TEXT:    cout << sqlite3_column_text(pStmt,i) ; break;
        case SQLITE_FLOAT:   cout << sqlite3_column_double(pStmt,i); break;
        case SQLITE_BLOB:    cout << "<blob>" ; break;
        case SQLITE_NULL:    cout << "-" ; break;
        default:             cout << "?" ; break;
      }
      if(i<cols-1) cout << separator; 
    }
    cout << endl;
    rc=sqlite3_step(pStmt);
  }
  sqlite3_reset(pStmt);
}

void Query::print(std::ostream& os) {
  os << sqlite3_expanded_sql(pStmt);
}

std::ostream& operator<<(std::ostream& stream, Query& statement) {
  stream << "query=" << statement.sql() << std::flush;
  return stream;
}

/*******************************************************************************
 * Database class functions
 ******************************************************************************/

// Open existing DB
Database::Database(const string& fn) {
  std::stringstream errmsg;
  int rc = sqlite3_open_v2(fn.c_str(), &db, SQLITE_OPEN_READWRITE, NULL);
  if(rc) { 
    errmsg << "Can't open database, filename: " << fn << ": " << (char *)sqlite3_errmsg(db);
    die(errmsg.str());
  }
  sql("select count(*) from sqlite_master");
  c_debug << "DB opened: " << fn << endl;  
}

int Database::createdb(const string& fn, const char* schema) {
  sqlite3* newdb; 
  char*    errmsg;
  string newname = parseFileName(fn);
  std::ifstream f(newname);
  if(!f.fail()) die("File exists: " + newname);
  int rc = sqlite3_open_v2(newname.c_str(), &newdb, SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE, NULL);
  if( rc != SQLITE_OK ) {
    std::stringstream errmsg;
    errmsg << "Can't create database, filename: " << newname << ": " << (char *)sqlite3_errmsg(newdb);
    die(errmsg.str());
  }
  // running vacuum on a new database ensures the database has the SQLite magic string instead of zero size
  rc = sqlite3_exec(newdb, "vacuum", 0, 0, &errmsg);
  if( rc != SQLITE_OK ) {
    std::cerr << "SQL error preparing new database " << newname << std::endl;
    die("Create database");
  }
  rc = sqlite3_exec(newdb, schema, 0, 0, &errmsg);
  if( rc != SQLITE_OK ) {
    std::cerr << "SQL error creating schema on " << newname << std::endl;
    die("Create schema");
  }
  return 0;
}

// detach tempdb and finalize all statements before closing db
Database::~Database()   { close(); }
void Database::vacuum() { sql("vacuum"); }

int Database::close() {
  int rc;
  c_debug << "Closing DB " << filename() << std::endl;
  if(db) rc=sqlite3_close(db);
  db=NULL;
  return rc;
}

const char* Database::filename() {
  if(!db) return NULL;
  return sqlite3_db_filename(db, NULL);
}

ulong Database::filesize() {
  if(!db) return 0;
  return fileSize(sqlite3_db_filename(db, NULL));
}

int Database::exists(const char* fn) {
  int rc = 0;
  sqlite3* tempdb;
  rc = sqlite3_open_v2(fn, &tempdb, SQLITE_OPEN_READONLY, NULL);
  if(rc==0) {
    sqlite3_close(tempdb);
    return 1;
  }
  return 0;
}

int Database::deletedb(const string& fn) {
  int rc = 0;
  sqlite3* tempdb;
  rc = sqlite3_open_v2(fn.c_str(), &tempdb, SQLITE_OPEN_READONLY, NULL);
  if(rc==0) {
    rc=sqlite3_close(tempdb);
    rc=unlink(fn.c_str());
  }
}

int Database::unlinkdb() {
  int rc=10;
  if(db) 
    rc=unlink(filename());
  return rc;
}

// Run SQL statement, ignore results (update/insert)
void Database::sql(const string& query) {
  //Stopwatch stopwatch;
  //showquery(query);
  // c_query << query;
  int rc=0;
  char * errmsg;
  rc = sqlite3_exec(db, query.c_str(), 0, 0, &errmsg);
  if( rc != SQLITE_OK ) {
    std::cerr << "SQL error: " << errmsg << ", SQL: " << query << std::endl;
    die("Cannot execute SQL");
  }
  //stopwatch.lap();
  //c_query << ", " << stopwatch.runtime() << std::endl;
}

/*******************************************************************************
 * Staging DB class functions
 ******************************************************************************/

void StagingDB::createdb(const string& fn, ulong blocksize) {
  Database::createdb(fn,R"(
PRAGMA journal_mode = off;
PRAGMA synchronous = off;
CREATE TABLE IF NOT EXISTS metadata(lock char(1) not null default 1
, blksz integer
, compression text
,constraint pk_t1 primary key(lock), constraint ck_t1_l check (lock=1));
CREATE TABLE IF NOT EXISTS files(id integer primary key autoincrement, name TEXT, hostname TEXT, timestamp integer, blocks integer, bytes integer);
CREATE TABLE IF NOT EXISTS staging(id integer primary key autoincrement default 0,hash integer, bytes integer);
CREATE TABLE IF NOT EXISTS testdata(k integer, b integer);
)");
  StagingDB newdb(fn);
  newdb.setblocksize(blocksize);
}

StagingDB::StagingDB(const string& fn): Database(fn),
  q_begin       (db,"begin transaction"),
  q_end         (db,"end transaction"),
  q_setblocksize(db,"insert into metadata (blksz,compression) values (?,'lz4')"),
  q_getblocksize(db,"select blksz from metadata"),
  q_getrows     (db,"select count(*) from staging"),
  q_insert      (db,"insert into staging(hash,bytes) values (?,?)"),
  q_meta        (db,"insert into files (name,blocks,hostname,timestamp,bytes) values (?,?,?,?,?)"),

  q_fillrand    (db,R"(
with recursive 
rnd(k,b) AS (
SELECT 1+ABS(RANDOM()%0xFFFFFFFFFFFFFF), 1+ABS(RANDOM())%?2 UNION ALL
SELECT 1+ABS(RANDOM()%0xFFFFFFFFFFFFFF), 1+ABS(RANDOM())%?2 FROM rnd LIMIT ?1
),
c(x) AS (SELECT 0 UNION ALL SELECT X+1 FROM C LIMIT ?3)
INSERT INTO STAGING SELECT NULL, K,B FROM rnd,c;
)"),

  q_fillzero(db,R"(
with recursive 
rnd(k,b) AS (
SELECT 0, 0 UNION ALL
SELECT 0, 0 FROM rnd LIMIT ?1
) INSERT INTO STAGING SELECT NULL, K,B FROM rnd;
)")
{
  sql("PRAGMA schema_version");      // trigger error if not open
  sql("PRAGMA journal_mode = off");
  sql("PRAGMA synchronous = off");
  /*
  sql("PRAGMA cache_spill = off");
  sql("PRAGMA cache_size = 10000");
  sql("PRAGMA locking_mode = EXCLUSIVE");
  */
  // mmap_size limits the amount of linux memory cache slots, this avoids slowing down large scans
  sql("PRAGMA mmap_size = 65536");
}

StagingDB::~StagingDB()  { }
void StagingDB::begin()  { q_begin.exec(); }
void StagingDB::end()    { q_end.exec(); }

ulong StagingDB::getblocksize() { return q_getblocksize.execl(); }
ulong StagingDB::getrows()      { return q_getrows.execl(); }
void StagingDB::setblocksize(ulong b) { q_setblocksize.bind(b); q_setblocksize.exec(); }

// insert hash, compressed bytes into staging
void StagingDB::insertkv(ulong hash, ulong bytes) {
  q_insert.bind(hash);
  q_insert.bind(bytes);
  q_insert.exec();
} 

int StagingDB::fillrandom(ulong rows, int blocksize, int dup) {
  begin();
  q_fillrand.bind(rows);
  q_fillrand.bind(blocksize*1024);
  q_fillrand.bind(dup);
  q_fillrand.execl();
  end();
  return 0;
}

int StagingDB::fillzero(ulong rows) {
  begin();
  q_fillzero.bind(rows);
  q_fillzero.execl();
  end();
  return 0;
}

int StagingDB::savemeta(string name, ulong blocks, ulong bytes) {
  q_meta.bind(name);
  q_meta.bind(blocks);
  q_meta.bind(hostName());
  q_meta.bind(starttime);
  q_meta.bind(bytes);
  q_meta.exec();
  return 0;
}

/*******************************************************************************
 * QDDA DB class functions
 ******************************************************************************/

QddaDB::QddaDB(const string& fn): Database(fn),
  q_getblocksize         (db,"select blksz from metadata"),
  q_getrows              (db,"select count(*) from kv"),
  q_getallocated         (db,"select sum(blocks) from v_compressed"),
  q_getarrayid           (db,"select arrayid from metadata"),
  q_getzero              (db,"select blocks from kv where hash=0"),
  q_gettotal             (db,"select sum(blocks) from kv"),
  q_getused              (db,"select sum(ref*count) from m_sums_deduped"),
  q_getnuniq             (db,"select sum(ref*count) from m_sums_deduped where ref>1"),
  q_getunique            (db,"select count from m_sums_deduped where ref=1"),
  q_getdeduped           (db,"select sum(count) from m_sums_deduped"),
  q_getbytescompressedraw(db,"select sum(raw) from m_sums_compressed"),
  q_getbytescompressednet(db,"select sum(bytes) from m_sums_compressed"),
  q_loadbuckets          (db,"insert or replace into buckets values (?)"),
  q_truncbuckets         (db,"delete from buckets"),
  q_listfiles            (db,"select * from v_files"),
  q_attach               (db,"attach database ? as ?"),
  q_detach               (db,"detach database ?"),
  q_trunc_sums_compr     (db,"delete from m_sums_compressed"),
  q_trunc_sums_deduped   (db,"delete from m_sums_deduped"),
  q_update_sums_compr    (db,"insert into m_sums_compressed select * from v_sums_compressed"),
  q_update_sums_deduped  (db,"insert into m_sums_deduped select * from v_sums_deduped"),
  q_hist_compress(db,"select * from v_compressed union all "
                     "select 'Total:', sum(buckets), sum(blocks),sum(MiB) from v_compressed"),
  q_hist_dedupe(db,"select * from v_deduped union all \n"
                   "select 'Total:',sum(blocks),sum(bytes),sum(MiB) from v_deduped")
{
  sql("PRAGMA schema_version");      // trigger error if not open
  sql("PRAGMA temp_store_directory = '" + tmpdir + "'");
  // sql("PRAGMA temp_store = 2"); // use memory for temp tables
}

// wrapper functions for prefab queries
ulong QddaDB::getblocksize()                      { return q_getblocksize.execl();  }  // blocksize from database
ulong QddaDB::getrows()                           { return q_getrows.execl(); }        // rows in kv table
ulong QddaDB::getallocated()                      { return q_getallocated.execl(); }  // blocks required after compress
ulong QddaDB::getzero()                           { return q_getzero.execl(); };
ulong QddaDB::gettotal()                          { return q_gettotal.execl(); };
ulong QddaDB::getunique()                         { return q_getunique.execl(); };
ulong QddaDB::getnuniq()                          { return q_getnuniq.execl(); };
ulong QddaDB::getdeduped()                        { return q_getdeduped.execl(); };
ulong QddaDB::getbytescompressedraw()             { return q_getbytescompressedraw.execl(); };
ulong QddaDB::getbytescompressednet()             { return q_getbytescompressednet.execl(); };
ulong QddaDB::getused()                           { return q_getused.execl(); };
void  QddaDB::report_files(const string& tabs)    { q_listfiles.report(tabs); };
void  QddaDB::report_dedupe(const string& tabs)   { q_hist_dedupe.report(tabs); };
void  QddaDB::report_compress(const string& tabs) { q_hist_compress.report(tabs); };
const string QddaDB::getarrayid() { return q_getarrayid.exectext(); }

void QddaDB::createdb(const string& fn) {
  Database::createdb(fn,R"(
CREATE TABLE IF NOT EXISTS metadata(lock char(1) not null default 1
, version text
, blksz integer
, compression text
, arrayid text
, created integer
, constraint pk_t1 primary key(lock), constraint ck_t1_l check (lock=1));

CREATE TABLE IF NOT EXISTS files(id integer primary key autoincrement
, name TEXT
, hostname TEXT
, timestamp integer
, blocks integer
, bytes integer);

CREATE TABLE IF NOT EXISTS kv(hash unsigned integer primary key, blocks integer, bytes integer) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS buckets(bucksz integer primary key NOT NULL);

CREATE VIEW IF NOT EXISTS v_files as
select id as file
, bytes/blocks as blksz
, blocks, bytes/1024/1024 as MiB
, strftime('%Y%m%d_%H%M', timestamp, 'unixepoch', 'utc') as date
, hostname || ':' || name as url 
from files;

CREATE VIEW IF NOT EXISTS v_sums_deduped as
select blocks ref, count(blocks) count
from kv where hash!=0 group by 1 order by ref;

CREATE VIEW IF NOT EXISTS v_sums_compressed as
-- select 0 size, blocks, 0 bytes, 0 rawbytes from kv where hash=999 union all
select ((bytes-1)/1024)+1 size,count(*) blocks, sum(bytes) bytes, sum(bytes*blocks) raw
from kv where hash!=0 group by (bytes-1)/1024;
-- select 0 bucksz, blocks, 0 bytes, 0 rawbytes from kv where hash=0 union all

CREATE TABLE m_sums_deduped as select * from v_sums_deduped where 1=0;
CREATE TABLE m_sums_compressed as select * from v_sums_compressed where 1=0;

CREATE VIEW IF NOT EXISTS v_bucket_compressed as 
select (select max(bucksz) from buckets) blksz
, (select min(bucksz) from buckets where bucksz >= m_sums_compressed.size) as size
, sum(blocks) sum 
from m_sums_compressed group by 2;

CREATE VIEW IF NOT EXISTS v_compressed as 
select size
, sum buckets
-- , size*sum bucket_kb
, (size*sum+blksz-1)/blksz blocks
, cast((((size*sum+blksz-1)/blksz)*blksz+1023)/1024 as float) MiB
from v_bucket_compressed;

CREATE VIEW IF NOT EXISTS v_deduped as
select 0 dup
, blocks
, (select blksz from metadata)*blocks*1024 bytes
, cast((select blksz from metadata)*blocks/1024 as real) MiB
from kv where hash=0 union all
select ref
, count*ref blocks
, (select blksz from metadata)*ref*count*1024 bytes
, cast((select blksz from metadata)*ref*count/1024 as real) MiB
from m_sums_deduped
)");

/* test stuff
INSERT INTO BUCKETS VALUES (2),(4),(8);
INSERT INTO KV VALUES (1,2,3);
INSERT INTO KV VALUES (4,5,3000);
INSERT INTO KV VALUES (5,5,5000);
*/

}

/*******************************************************************************
 * Default metadata settings for each compression method
 * Currently the methods are:
 * X1: Bucketsizes for XtremIO X1,   8K blocksize
 * X2: Bucketsizes for XtremIO X2,  16K blocksize
 * V1: VMAX All Flash (beta),      128K blocksize
 * Other methods can be loaded manually into the database after creating the DB
 ******************************************************************************/

void QddaDB::setmetadata(int blocksize, const string& compr, const string& name, const string& buckets) {
  if(blocksize>128) die("Blocksize too large");
  Query q_meta(db,"insert into metadata (version, blksz, compression, arrayid, created) values (?,?,?,?,?)");
  q_meta.bind(PROGVERSION);  
  q_meta.bind(blocksize);
  q_meta.bind(compr);
  q_meta.bind(name);
  q_meta.bind(epoch());
  q_meta.exec();
  loadbuckets(buckets);
}

void QddaDB::parsemetadata(string pr) {
  if(getblocksize()) return;
  if     (pr == "x1")    setmetadata(  8, "lz4", "XtremIO X1", "2+4+8");
  else if(pr == "x2")    setmetadata( 16, "lz4", "XtremIO X2", "1+2+3+4+5+6+7+8+9+10+11+12+13+15+16");
  else if(pr == "vmax1") setmetadata(128, "lz4", "VMAX v1",    "8+16+24+32.40+48+56+64+72+80+88+96+104+112+120+128");
  else {
    string buckets, str;
    string name = "custom";
    int    blocksize = 16;
    std::stringstream ss(pr);
    while(ss.good()) {
      getline(ss,str,',');
      if(str.substr(0,5) == "name=")    name=str.substr(5);
      if(str.substr(0,3) == "bs=")      blocksize=atoll(str.substr(3).c_str());
      if(str.substr(0,8) == "buckets=") buckets=str.substr(8);
    }
    setmetadata(blocksize, "lz4", name, buckets);
  }
}

void QddaDB::loadbuckets(const string& s) {
  q_truncbuckets.execl();
  q_loadbuckets.bind(0ul);
  q_loadbuckets.execl();
  std::stringstream ss(s);
  string str;
  while(ss.good()) {
    getline(ss,str,'+');
    q_loadbuckets.bind(atoll(str.c_str()));
    q_loadbuckets.execl();
  }
  q_loadbuckets.bind(getblocksize());
  q_loadbuckets.execl();
}

/*
for reference, classic merge:
insert or replace into kv
select tmpdb.staging.k, coalesce(kv.v,0)+count(tmpdb.staging.k), tmpdb.staging.b from tmpdb.staging
left outer join kv on kv.k = tmpdb.staging.k
group by tmpdb.staging.k
*/

// merge staging data into main table (performance optimized)
void  QddaDB::merge(const string& name) {
  q_attach.bind(name);
  q_attach.bind("tmpdb");
  q_attach.exec();
  Query q_merge(db,"with t(hash,blocks,bytes) as ("
                   "select hash,blocks,bytes from kv union all "
                   "select hash,1,bytes from tmpdb.staging"
                   ") insert or replace into kv "
                   "select hash,sum(blocks),bytes from t group by hash order by hash");
  Query q_copy(db, "insert into files (name,hostname,timestamp,blocks,bytes) "
                   "select name,hostname,timestamp,blocks,bytes from tmpdb.files");
  q_merge.exec();
  q_copy.exec();
  q_trunc_sums_compr.exec();
  q_trunc_sums_deduped.exec();
  q_update_sums_compr.exec();
  q_update_sums_deduped.exec();
} 

void QddaDB::import(const string& fn) {
  q_attach.bind(fn);
  q_attach.bind("impdb");
  q_attach.exec();
  Query q_import(db,"insert or replace into main.kv \n"
                    "select impdb.kv.hash \n"
                    ", coalesce(main.kv.blocks,0) + impdb.kv.blocks\n"
                    ", impdb.kv.bytes from impdb.kv\n"
                    "left outer join main.kv on main.kv.hash = impdb.kv.hash\n"
                    "group by impdb.kv.hash\n"
                    "order by main.kv.hash,impdb.kv.hash\n");
  q_import.exec();
  q_trunc_sums_compr.exec();
  q_trunc_sums_deduped.exec();
  q_update_sums_compr.exec();
  q_update_sums_deduped.exec();
  sql("insert into files(name, hostname, timestamp, blocks, bytes) "
      "select name, hostname, timestamp, blocks, bytes from impdb.files");
}

