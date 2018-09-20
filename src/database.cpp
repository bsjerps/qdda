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
#include <unistd.h>

#include "sqlite/sqlite3.h"

#include "error.h"
#include "tools.h"
#include "database.h" 
#include "qdda.h"

extern bool g_debug;
extern bool g_query;
extern const char* PROGVERSION;
extern ulong starttime;

using std::string;

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

// wrapper for calls with string instead of char*
int fileExists(const string& fn)        { return fileExists(fn.c_str());}
int fileIsSqlite3(const string& fn)     { return fileIsSqlite3(fn.c_str());}
int fileDeleteSqlite3(const string& fn) { return fileDeleteSqlite3(fn.c_str()); }

/*******************************************************************************
 * Query class functions
 ******************************************************************************/

Query::Query(Database& db, const char* query) { init(db.db, query) ;}
Query::Query(sqlite3* db, const char* query)  { init(db, query) ;}

void Query::init(sqlite3* db, const char* query) {
  ref = 0; // reset parameter count
  if(!db) throw ERROR("sqlite3 not initialized ") << query;
  const char * pzTest;
  int rc=sqlite3_prepare_v2(db, query, strlen(query), &pStmt, &pzTest);
  if(rc==SQLITE_OK) return;
  throw ERROR("Peparing MySQL query:") << query << ", " << sqlite3_errmsg(db);
}

Query::~Query()               { sqlite3_finalize(pStmt);}
const char* Query::sqlerror() { return sqlite3_errmsg(sqlite3_db_handle(pStmt)); }
const char* Query::sql()      { return sqlite3_sql(pStmt); }

int Query::bind() {
  int rc = sqlite3_bind_null(pStmt, ++ref);
  if(rc!=SQLITE_OK) throw ERROR("MySQL bind variable NULL failed, query: ") << sql() << ", " << sqlerror();
  return 0;
}  
int Query::bind(const ulong p) { 
  int rc = sqlite3_bind_int64(pStmt, ++ref, p);
  if(rc!=SQLITE_OK) throw ERROR("MySQL bind variable ulong failed, query: ") << sql() << ", " << sqlerror();
  return 0;
};

int Query::bind(const char* p) {
  if(!p) throw ERROR("SQLite parameter is NULL pointer");
  int rc = sqlite3_bind_text (pStmt, ++ref, p, strlen(p),SQLITE_STATIC);
  if(rc!=SQLITE_OK) throw ERROR("MySQL bind string failed, query: ") << sql() << ", " << sqlerror();
  return 0;
};

int Query::bind(const string& p) { return bind(p.c_str()); };

int Query::step() {
  if(!pStmt) throw ERROR("Query statement not prepared");
  if(g_query) print(std::cout);
  int rc = sqlite3_step(pStmt);
  if(rc==SQLITE_DONE) return 0;
  if(rc==SQLITE_ROW) return 0;
  throw ERROR("executing SQL statement ") << sql() << ", " << sqlerror();
  return 0; // we never get here
}

int Query::reset() {
  if(g_query) std::cout << "\n" << std::flush;
  ref=0;
  int rc = sqlite3_reset(pStmt);
  if(rc!=SQLITE_OK) throw ERROR("MySQL reset failed, ") << sqlerror();
  return 0;
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
  // handle multiple rows returned (SQLITE_DONE) ?
  reset();
  return retval;
}

ulong Query::execl(ulong p1)           { bind(p1); return execl(); }
ulong Query::execl(ulong p1, ulong p2) { bind(p1);bind(p2); return execl(); }

// simple exec of prepared statement, float return value
float Query::execf() {
  step();
  float retval = sqlite3_column_double(pStmt, 0);
  if(g_query) std::cout << " = " << retval;
  // handle multiple rows returned (SQLITE_DONE) ?
  reset();
  return retval;
}

// simple exec of prepared statement, string return value
const string Query::execstr() {
  string retval;
  const unsigned char * pText = NULL;
  step();
  pText = sqlite3_column_text(pStmt, 0);
  retval = (char*) pText;

  if(g_query) std::cout << " = " << retval;
  // handle multiple rows returned (SQLITE_DONE) ?
  reset();
  return retval;
}

void Query::report(std::ostream& os, const string& tabstr) {
  char separator = ' ';
  std::vector<int>  tabs;
  int               rc = 0;  
  string            str;
  std::stringstream ss(tabstr);
  
  while(ss.good()) {
    getline(ss,str,',');
    tabs.push_back(atoi(str.c_str()));
  }
  if(g_query) { print(std::cout); std::cout << std::endl; }
  int cols = sqlite3_column_count(pStmt);
  int i;
  for(i=0;i<cols; i++) {
    if(tabs[i]==0) break;
    if(tabs[i]>0) std::cout << std::left;
    else os << std::right;
    if(i<tabs.size()) os << std::setw(abs(tabs[i]));
    os << sqlite3_column_name(pStmt,i);
    if(i<cols-1) os << separator;
  }
  if(i) os << "\n";

  rc=sqlite3_step(pStmt);
  while(rc==SQLITE_ROW) {
    for(i=0;i<cols;i++) {
      if(tabs[i]>0) os << std::left;
      else os << std::right;
      int coltype = sqlite3_column_type(pStmt,i);
      if(i<tabs.size()) os << std::setw(abs(tabs[i]));
      os << std::setprecision(2) << std::fixed;
      switch(coltype) {
        case SQLITE_INTEGER: os << sqlite3_column_int64(pStmt,i) ; break;
        case SQLITE_TEXT:    os << sqlite3_column_text(pStmt,i) ; break;
        case SQLITE_FLOAT:   os << sqlite3_column_double(pStmt,i); break;
        case SQLITE_BLOB:    os << "<blob>" ; break;
        case SQLITE_NULL:    os << "-" ; break;
        default:             os << "?" ; break;
      }
      if(i<cols-1) os << separator; 
    }
    os << "\n";
    rc=sqlite3_step(pStmt);
  }
  sqlite3_reset(pStmt);
}

void Query::print(std::ostream& os) {
  os << sqlite3_expanded_sql(pStmt);
}

Query& Query::operator<< (ulong p)         { bind (p); return *this; }
Query& Query::operator<< (uint32_t p)      { bind (ulong(p)); return *this; }
Query& Query::operator<< (const char* p)   { bind (p); return *this; }
Query& Query::operator<< (const string& p) { bind (p); return *this; }

/*******************************************************************************
 * Database class functions
 ******************************************************************************/

// Open existing DB
Database::Database(const string& fn) {
  int rc = sqlite3_open_v2(fn.c_str(), &db, SQLITE_OPEN_READWRITE, NULL);
  if(rc) throw ERROR("Can't open database, ") << sqlite3_errmsg(db);
  sql("select count(*) from sqlite_master");
  if(g_debug) std::cerr << "DB opened: " << fn << std::endl;
}

int Database::createdb(const string& fn, const char* schema) {
  sqlite3* newdb; 
  char*    errmsg;
  std::ifstream f(fn);

  if(access(fn.c_str(), F_OK | R_OK)==0) throw ERROR("File already exists: ") << fn ;

  int rc = sqlite3_open_v2(fn.c_str(), &newdb, SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE, NULL);
  if( rc != SQLITE_OK ) throw ERROR("Can't create database, ") << (char *)sqlite3_errmsg(newdb);
  // running vacuum on a new database ensures the database has the SQLite magic string instead of zero size
  rc = sqlite3_exec(newdb, "vacuum", 0, 0, &errmsg);
  if( rc != SQLITE_OK ) throw ERROR("Initializing database ") << fn << ", " << sqlite3_errmsg(newdb);

  rc = sqlite3_exec(newdb, schema, 0, 0, &errmsg);
  if( rc != SQLITE_OK ) throw ERROR("Creating schema failed on ") << fn << ", " << sqlite3_errmsg(newdb);
  return 0;
}

// detach tempdb and finalize all statements before closing db
Database::~Database()   { close();  }
void Database::begin()  { sql("begin"); }
void Database::end()    { sql("end"); }

int Database::attach(const string& schema, const string& fname) {
  Query q(db, "attach database ? as ?");
  q << fname << schema;
  q.exec();
  return 0;
}

int Database::detach(const string& schema) {
  Query q(db, "detach database ?");
  q << schema;
  q.exec();
  return 0;
}

void Database::vacuum() { sql("vacuum"); }

int Database::close() {
  int rc = 0;
  if(g_debug) std::cerr << "Closing DB " << filename() << std::endl;
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

int Database::exists(const string& fn) {
  int rc = 0;
  sqlite3* tempdb;
  rc = sqlite3_open_v2(fn.c_str(), &tempdb, SQLITE_OPEN_READONLY, NULL);
  if(rc==0) {
    sqlite3_close(tempdb);
    return 1;
  }
  return 0;
}

int Database::deletedb(const string& fn) {
  int rc;
  sqlite3* tempdb;
  rc = sqlite3_open_v2(fn.c_str(), &tempdb, SQLITE_OPEN_READONLY, NULL);
  if(rc==0) {
    sqlite3_close(tempdb);
    rc=unlink(fn.c_str());
  }
  return rc;
}

int Database::unlinkdb() {
  int rc=10;
  if(db) 
    rc=unlink(filename());
  return rc;
}

// Run SQL statement, ignore results (update/insert)
void Database::sql(const char* query) {
  int rc=0;
  char * errmsg;
  rc = sqlite3_exec(db, query, 0, 0, &errmsg);
  if( rc != SQLITE_OK ) {
    throw ERROR("Cannot execute SQL, ") << query << ", " << sqlite3_errmsg(db);
  }
}

void Database::sql(const string& query) { sql(query.c_str()); }

ulong Database::getul(const char* query) {
  Query q(db,query);
  return q.execl();
}

float Database::getfl(const char* query) {
  Query q(db,query);
  return q.execf();
}

const string Database::getstr(const char* query) {
  Query q(db,query);
  return q.execstr();
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
CREATE TABLE IF NOT EXISTS staging(id integer primary key autoincrement, hash integer, bytes integer);
CREATE VIEW IF NOT EXISTS offsets as with m(b) as (select blksz from metadata) select hash, printf('%0#16x',hash) hexhash, (id-1) offset, (id-1) * m.b*1024 bytes from staging,m
)");
  StagingDB newdb(fn);
  newdb.setblocksize(blocksize);
}

StagingDB::StagingDB(const string& fn): Database(fn),
  q_insert      (*this,"insert into staging(hash,bytes) values (?,?)")
{
  sql("PRAGMA schema_version");      // trigger error if not open
  sql("PRAGMA journal_mode = off");  // speed up, don't care about consistency
  sql("PRAGMA synchronous = off");   // same
  
  //sql("PRAGMA cache_spill = off");
  //sql("PRAGMA cache_size = 10000");
  //sql("PRAGMA locking_mode = EXCLUSIVE");

  // mmap_size limits the amount of linux memory cache slots, 
  // this avoids slowing down large scans
  sql("PRAGMA mmap_size = 65536");
}

ulong StagingDB::blocksize() { return getul("select blksz from metadata"); }
ulong StagingDB::getrows()   { return getul("select count(*) from staging"); }

void StagingDB::setblocksize(ulong p) {
  Query q(*this,"insert into metadata (blksz,compression) values (?,'dummy')");
  q << p;
  q.exec();
}

StagingDB::~StagingDB()  { }

// insert hash, compressed bytes into staging
void StagingDB::insertdata(ulong hash, ulong bytes) {
  q_insert.bind(hash);
  if(bytes!=-1) q_insert.bind(bytes);
  else q_insert.bind();
  q_insert.exec();
} 

int StagingDB::fillrandom(ulong rows, int blocksize, int dup) {
  Query q(*this,R"(
with recursive 
rnd(k,b) AS (
SELECT 1+ABS(RANDOM()%0xFFFFFFFFFFFFFF), 1+ABS(RANDOM())%?2 UNION ALL
SELECT 1+ABS(RANDOM()%0xFFFFFFFFFFFFFF), 1+ABS(RANDOM())%?2 FROM rnd LIMIT ?1
),
c(x) AS (SELECT 0 UNION ALL SELECT X+1 FROM C LIMIT ?3)
INSERT INTO STAGING SELECT NULL, K,B FROM rnd,c;
)");
  begin();
  q << rows;
  q << (ulong)blocksize*1024;
  q << (ulong)dup;
  q.exec();
  end();
  return 0;
}

int StagingDB::fillzero(ulong rows) {
  Query q(*this,R"(
with recursive 
rnd(k,b) AS (
SELECT 0, 0 UNION ALL
SELECT 0, 0 FROM rnd LIMIT ?1
) INSERT INTO STAGING SELECT NULL, K,B FROM rnd;
)");
  begin();
  q << rows;
  q.exec();
  end();
  return 0;
}

int StagingDB::insertmeta(const string& name, ulong blocks, ulong bytes) {
  Query q(*this,"insert into files (name,blocks,hostname,timestamp,bytes) values (?,?,?,?,?)");
  q << name << blocks << hostName() << starttime << bytes;
  q.exec();
  return 0;
}

/*******************************************************************************
 * QDDA DB class functions
 ******************************************************************************/

QddaDB::QddaDB(const string& fn): Database(fn) {
  sql("PRAGMA schema_version");      // trigger error if not open
  sql("PRAGMA temp_store_directory = '" + tmpdir + "'");
  // sql("PRAGMA temp_store = 2"); // use memory for temp tables
  sql("PRAGMA journal_mode = off");  // speed up, don't care about consistency
  sql("PRAGMA synchronous = off");   // same
}

void QddaDB::squash()     { sql("update kv set blocks=1"); update(); }
ulong QddaDB::blocksize() { return getul("select blksz from metadata"); }
ulong QddaDB::getrows()   { return getul("select count(*) from kv"); }
const string QddaDB::getarrayid() { return getstr("select arrayid from metadata"); }
  
void QddaDB::createdb(const string& fn) {
  Database::createdb(fn,R"(
CREATE TABLE IF NOT EXISTS metadata(lock char(1) not null default 1
, version text
, blksz integer
, compression text check (compression in ('none', 'lz4', 'deflate')) default 'none'
, level integer
, arrayid text check (arrayid in ('custom', 'x1', 'x2', 'vmax1')) default 'none'
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
select blocks ref, count(blocks) blocks
from kv where hash!=0 group by 1 order by ref;

CREATE VIEW IF NOT EXISTS v_sums_compressed as
select ((bytes-1)/1024)+1 size,count(*) blocks
, sum(blocks) totblocks
, sum(bytes) bytes
, sum(bytes*blocks) raw
from kv where hash!=0 and bytes not NULL group by (bytes-1)/1024;

CREATE TABLE m_sums_deduped as select * from v_sums_deduped where 1=0;
CREATE TABLE m_sums_compressed as select * from v_sums_compressed where 1=0;

CREATE VIEW IF NOT EXISTS v_bucket_compressed as
WITH data(blksz,total) as (select (select max(bucksz) from buckets),(select sum(blocks) from m_sums_compressed))
select 
(select min(bucksz) from buckets where bucksz >= m_sums_compressed.size) size
, blksz
, total
, sum(blocks) blocks
from m_sums_compressed,data group by 1;

CREATE VIEW IF NOT EXISTS v_deduped as
WITH data(blksz,sum) as (select (select blksz*1024 from metadata),(select sum(blocks) from kv))
select 0 dup
, blocks
, 100.0*blocks/sum perc
, blksz*blocks/1048576.0 MiB
from kv,data where hash=0 union all
select ref
, blocks*ref blocks
, 100.0*blocks*ref/sum
, blksz*ref*blocks/1048576.0 MiB
from m_sums_deduped,data;

CREATE VIEW IF NOT EXISTS v_compressed as
WITH temp(size, blksz, buckets, blocks, perc) as (
select size, blksz, blocks, (size*blocks+blksz-1)/blksz, 100.0*blocks/total
from v_bucket_compressed)
select size, buckets, buckets*blksz/1024.0 RawMiB, perc, blocks, blocks*blksz/1024.0 MiB from temp;
)");
}

/*******************************************************************************
 * Default metadata settings for each compression method
 * Currently the methods are:
 * X1: Bucketsizes for XtremIO X1,   8K blocksize
 * X2: Bucketsizes for XtremIO X2,  16K blocksize
 * V1: VMAX All Flash (beta),      128K blocksize
 * Other methods can be loaded manually into the database after creating the DB
 ******************************************************************************/

void QddaDB::setmetadata(int blocksz, const char* compr, const char* name, const string& buckets) {
  if(blocksize()) return; // do nothing if metadata is already set
  if(blocksz>128) throw ERROR("Blocksize too large: ") << blocksz;
  Query q_meta(*this,"insert into metadata (version, blksz, compression, arrayid, created) values (?,?,?,?,?)");
  q_meta << PROGVERSION << (ulong)blocksz << compr << name << epoch();
  q_meta.exec();
  loadbuckets(buckets);
}

void QddaDB::loadbuckets(const string& s) {
  Query loadbuckets(*this,"insert or replace into buckets values (?)");
  Query truncbuckets(*this,"delete from buckets");
  truncbuckets.exec();
  loadbuckets.bind(0ul);
  loadbuckets.exec();
  std::stringstream ss(s);
  string str;
  while(ss.good()) {
    getline(ss,str,',');
    loadbuckets.bind(atoll(str.c_str()));
    loadbuckets.exec();
  }
  loadbuckets.bind((int)blocksize());
  loadbuckets.exec();
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
  attach("tmpdb",name);

  Query q_merge(db,"with t(hash,blocks,bytes) as ("
                   "select hash,blocks,bytes from kv union all "
                   "select hash,1,bytes from tmpdb.staging"
                   ") insert or replace into kv "
                   "select hash,sum(blocks),bytes from t group by hash"); //  order by hash
  Query q_copy(db, "insert into files (name,hostname,timestamp,blocks,bytes) "
                   "select name,hostname,timestamp,blocks,bytes from tmpdb.files");
  q_merge.exec();
  q_copy.exec();
  
  detach("tmpdb");
  update();
}

void QddaDB::update() {
  sql("delete from m_sums_compressed;\n"
      "delete from m_sums_deduped;\n"
      "insert into m_sums_compressed select * from v_sums_compressed;\n"
      "insert into m_sums_deduped select * from v_sums_deduped;\n");
}
      
void QddaDB::import(const string& fn) {
  attach("impdb",fn);
  sql("insert or replace into main.kv \n"
      "select impdb.kv.hash \n"
      ", coalesce(main.kv.blocks,0) + impdb.kv.blocks\n"
      ", impdb.kv.bytes from impdb.kv\n"
      "left outer join main.kv on main.kv.hash = impdb.kv.hash\n"
      "group by impdb.kv.hash\n"
      "order by main.kv.hash,impdb.kv.hash\n");
  sql("insert into files(name, hostname, timestamp, blocks, bytes) "
      "select name, hostname, timestamp, blocks, bytes from impdb.files");
  update();
  detach("impdb");
}

