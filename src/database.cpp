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
#include <array>
#include <unistd.h>
#include <sqlite3.h>
#include <errno.h>
#include <err.h>

#include "tools.h"
#include "qdda.h"

extern std::ofstream  o_debug;
extern std::ofstream  o_query;
extern sqlitedb       dbase;
extern std::string    database_fn;
extern char*          deftmppath;

/*******************************************************************************
 * SQL complex query text
 ******************************************************************************/

// blocks required for each bucket size
const char * sql_buckets = R"(select count(b)*max/(select blksz from metadata) from buckets
left outer join kv on kv.b between min and max where buckets.type=?1 group by max)";

// Table of Bucketsize, Buckets, Blocks and Bytes for each bucket size (compression histogram)
const char * sql_chist = R"(select max/1024 as 'Bucket(KiB)', count(b) as buckets,count(b)*max as Bytes from buckets
left outer join kv on kv.b between min and max where buckets.type=?
group by max,max
)";

/*******************************************************************************
 * Generic functions
 ******************************************************************************/
void showquery(const std::string& query) {
  o_query << "query=\"" << query << "\" " << std::flush;
}

// test if specified file is SQLite3 file
int file_sqlite3(const char * fn) {
  const char * magic = "SQLite format 3";
  char buf[32];
  std::ifstream f(fn);
  if (!f.good()) return 0;           // can't open file - nonexistent?
  f.seekg (0, f.beg);                // start at begin of file
  f.read (buf,strlen(magic));        // read the magic string
  buf[strlen(magic)]=0;              // force terminate with \0
  if(strcmp(magic,buf)==0) return 1; // compare with magic
  return 0;
}

// delete file only if it exists and is an SQLite 3 database
int sqlite_delete(const char * fn) {
  if(!file_exists(fn))  return 1;
  if(!file_sqlite3(fn)) return 1;
  o_debug << "Deleting file " << fn << std::endl;
  if (unlink(fn)) err(1, "%s", fn);
  return 0;
}

// wrapper for calls with string instead of char*
int file_exists(const std::string& fn)  { return file_exists(fn.c_str());}
int file_sqlite3(const std::string& fn) { return file_sqlite3(fn.c_str());}

// safety guards against overwriting existing files or devices by SQLite
std::string parsefilename(const std::string& in) {
  std::string fn;
  char cwd[80];
  if(!getcwd(cwd, 80)) die("Get CWD failed");
  if((signed char)in.c_str()[0] != '/') { fn = cwd ; fn += '/' ;} // prepend cwd if fn is relative
  fn+=in;                                                         // append input filename
  if (!fn.compare(0,4,"/dev")  )           { die("/dev not allowed in filename: " + fn);}
  if (!fn.compare(0,5,"/proc") )           { die("/proc not allowed in filename: " + fn);}
  if (!fn.compare(0,4,"/sys")  )           { die("/sys not allowed in filename: " + fn);}
  if (!fn.find_last_of("/\\"))             { die("root filesystem not allowed: " + fn);}
  if (fn[fn.length()-1] == '/')            { die("Is directory: " + fn);}
  if(file_exists(fn) && !file_sqlite3(fn)) { die("Not a SQlite3 file: " + fn);}
  return fn;
}

/*******************************************************************************
 * Database class functions
 ******************************************************************************/

statement::statement() { stmt = NULL; } // initialize stmt

// dump error if we forget to clean up our query
statement::~statement() {
  if(stmt) die("SQL query not finalized, query= " + std::string(sqlite3_sql(stmt)));
}

// prepare the statement
int statement::prepare(sqlite3 *db, const char * query) {
  int rc=0;
  const char * pzTest;
  rc=sqlite3_prepare_v2(db, query, strlen(query), &stmt, &pzTest);
  if(rc!=SQLITE_OK) die("SQL Prepare error " + to_string(rc) + ", query: " + query);
  return rc;
}

// execute prepared statement, return ulong
ulong statement::exec(const ulong p1) {
  if(!stmt) die("Query statement not prepared");
  showquery(sql());
  stopwatch_t stopwatch;
  ulong retval = 0;
  sqlite3_bind_int64(stmt, 1, p1);
  sqlite3_step(stmt);
  retval = sqlite3_column_int64(stmt, 0);
  sqlite3_reset(stmt);
  stopwatch.lap();
  o_query << ", " << stopwatch.runtime() << std::endl;
  return retval;
}

// same with 2 ulong parameters
// no performance profiling because this one needs to be faaaast
ulong statement::exec(const ulong p1, const ulong p2) {
  if(!stmt) die("Query statement not prepared");
  ulong retval = 0;
  sqlite3_bind_int64(stmt, 1, p1);
  sqlite3_bind_int64(stmt, 2, p2);
  sqlite3_step(stmt);
  retval = sqlite3_column_int64(stmt, 0);
  sqlite3_reset(stmt);
  return retval;
}

// same with 1 string and 2 units
ulong statement::exec(const char * p1,const ulong p2, const ulong p3) {
  if(!stmt) die("Query statement not prepared");
  showquery(sql());
  stopwatch_t stopwatch;
  sqlite3_reset(stmt);
  sqlite3_bind_text (stmt, 1, p1, strlen(p1),SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 2, p2);
  sqlite3_bind_int64(stmt, 3, p3);
  sqlite3_step(stmt);
  stopwatch.lap();
  o_query << ", " << stopwatch.runtime() << std::endl;
  return sqlite3_column_int64(stmt, 0);
}

// same but count the ulong sum of multiple rows, 1 parameter
ulong statement::execsum(const char * p1) {
  if(!stmt) die("Query statement not prepared");
  showquery(sql());
  stopwatch_t stopwatch;
  int rc=0;
  ulong retval = 0;
  sqlite3_bind_text (stmt, 1, p1, strlen(p1),SQLITE_STATIC);
  rc=sqlite3_step(stmt);
  while(rc==SQLITE_ROW) { 
    retval += sqlite3_column_int64(stmt, 0);
    rc=sqlite3_step(stmt);
  }
  stopwatch.lap();
  sqlite3_reset(stmt);
//  ulong h = hash6_md5(p1, strlen(p1)) & 0xFFFF;
  o_query << ", " << stopwatch.runtime() << std::endl;
  return retval;
}

// cleanup
void statement::finalize() {
  sqlite3_finalize(stmt);
  stmt = NULL;
}

// Initialize with closed db
sqlitedb::sqlitedb() {
  tmpdir = deftmppath;  
  db = NULL;
}

// detach tempdb and finalize all statements before closing db
sqlitedb::~sqlitedb() {
  detach();
  stmt_insert.finalize();
  stmt_meta.finalize();
  stmt_bucket.finalize();
  stmt_buckets.finalize();
  if(db) sqlite3_close(db);
}

/*******************************************************************************
 * Load default bucket sizes into database for each compression method
 * Currently the methods are:
 * X1: Bucketsizes for XtremIO X1,   8K blocksize
 * X2: Bucketsizes for XtremIO X2,  16K blocksize
 * V1: VMAX All Flash (beta),      128K blocksize
 * Other methods can be loaded manually into the database after creating the DB
 ******************************************************************************/

void sqlitedb::loadbuckets() {
  ulong prev;

  std::array<int,3>  x1 {2,4,8};
  std::array<int,15> x2 {1,2,3,4,5,6,7,8,9,10,11,12,13,15,16};
  std::array<int,15> v1 {8,16,24,32,40,48,56,64,72,80,88,96,104,112,128};

  prev=0 ; for (uint i=0; i<x1.size(); ++i)  { insbucket("X1",prev+1,x1[i]*1024); prev=x1[i]*1024; }
  prev=0 ; for (uint i=0; i<x2.size(); ++i)  { insbucket("X2",prev+1,x2[i]*1024); prev=x2[i]*1024; }
  prev=0 ; for (uint i=0; i<v1.size(); ++i)  { insbucket("V1",prev+1,v1[i]*1024); prev=v1[i]*1024; }
    
  sql("insert or replace into bucketdesc values ('','None')");
  sql("insert or replace into bucketdesc values ('X1','XtremIO X1')");
  sql("insert or replace into bucketdesc values ('X2','XtremIO X2')");
  sql("insert or replace into bucketdesc values ('V1','VMAX AFA v1 (experimental)')");
}

// Open database and create tables if needed
int sqlitedb::open(const std::string& fn,ulong new_blksz) {
  if(db) return 0; // do nothing if database already open
  int rc = 0;
  attached = 0;
  std::stringstream errmsg;
  std::string dbfilename = parsefilename(fn);
  rc = sqlite3_open(fn.c_str(), &db);
  if(rc) {
    errmsg << "Can't open database, filename: " << fn << ": " << (char *)sqlite3_errmsg(db);
    die (errmsg.str());
  } else {
    sql("PRAGMA schema_version");      // trigger error if not open
    sql("PRAGMA cache_size = 131072"); // 1GB memory for buffers
    schema();
    sql("PRAGMA journal_mode = off");
    sql("PRAGMA threads = 2");
    sql("PRAGMA synchronous = off");
    sql("PRAGMA temp_store_directory = '" + tmpdir + "'");
    // sql("PRAGMA temp_store = 2"); // use memory for temp tables
    stmt_meta.prepare(db,"insert into files(name,blocks,size) values (?,?,?)");
    stmt_bucket.prepare(db,"insert or replace into buckets values (?,?,?)");
    stmt_buckets.prepare(db,sql_buckets);
    auto blksz = blocksize();
    if(!blksz) sql("insert into metadata (blksz) values (" + to_string(new_blksz,0) + ")" );
    loadbuckets();
    o_debug << "DB opened: " << fn << std::endl;
  }
  return(rc);
}


/*******************************************************************************
 * SQLite stuff
 ******************************************************************************/

// Create and/or attach temp staging database
int sqlitedb::attach() {
  if(!attached) {
    const std::string tmpfilename = tmpdir + "/" + "qdda-staging.db" ;
    tmpfn = parsefilename(tmpfilename);
    sql("attach database '" + tmpfn +"' as tmpdb");
    sql("CREATE TABLE IF NOT EXISTS tmpdb.staging(k integer, b integer)");
    sql("PRAGMA tmpdb.journal_mode = off");
    sql("PRAGMA tmpdb.synchronous = off");
    stmt_insert.prepare(db,"insert into tmpdb.staging(k,b) values (?,?)");
    attached = 1;
  }
  return 0;
}

// return filename of tmpdb
const char * sqlitedb::tmpfilename() {
  // sqlite3_db_filename() doesn't work in older SQLite
  if(attached) return tmpfn.c_str();
  else return "";
};

// detach tmpdb, delete if d=true
void sqlitedb::detach(bool d) {
  std::string tmpfile;
  if(attached) {
    tmpfile = dbase.tmpfilename();
    stmt_insert.finalize();
    sql("detach tmpdb");
    attached=0;
    if(d) sqlite_delete(tmpfile.c_str());
  }
}

/* non-optimized (prepared) SQL, don't use inside long loops */

// Run SQL statement, ignore results (update/insert)
void sqlitedb::sql(const std::string& query) {
  stopwatch_t stopwatch;
  showquery(query);
  int rc=0;
  char * errmsg;
  rc = sqlite3_exec(db, query.c_str(), 0, 0, &errmsg);
  if( rc != SQLITE_OK ) {
    std::cerr << "SQL error: " << errmsg << ", SQL: " << query << std::endl;
    die("Cannot execute SQL");
  }
  stopwatch.lap();
  o_query << ", " << stopwatch.runtime() << std::endl;
}

// SQLite3 callback function - fills struct qresult with header and query result
// format it with qresult::tabs
// Need to rewrite this with better format handling

int sqlitedb::callback(void *r, int argc, char **argv, char **azColName) {
   qresult *qr = (qresult *)r;
   std::stringstream h,d;
   if(qr->header.empty()) {       // first call - display header
     for(int i=0; i<argc; i++) {
       if(qr->tabs[i])h << std::setw(abs(qr->tabs[i]));
       if(qr->tabs[i]<0) h << std::right; else h << std::left;
       h << azColName[i] << " ";
     }
     qr->header = h.str();
   }
   if(!qr->data.empty()) qr->data += "\n"; // add row for each row in selection, tab separated
   for(int i=0; i<argc; i++) {
     if(qr->tabs[i])   d << std::setw(abs(qr->tabs[i]));
     if(qr->tabs[i]<0) d << std::right; else d << std::left;
     d <<(const char *)(argv[i]?argv[i]:"NULL") << " ";
   }
   qr->data += d.str();
   return 0;
}

// Run query and show header and rows
int sqlitedb::runquery(const char * tabstr,const std::string& query) {
  const int maxtabs = 32;
  qresult r;                                                // result struct to pass to callback
  int tabs[maxtabs];for (int i=0;i<maxtabs;i++) tabs[i]=0;  // array of tab stops
  char buf[maxtabs*8];
  const char * token;
  strcpy(buf,tabstr);
  showquery(query);
  for(int i=0;i<maxtabs;i++) {                              // split tabstring into array of ints
    token=strtok(i?NULL:buf,",");
    if(!token) break;
    tabs[i]=atoi(token);
  }
  r.tabs = tabs;                                            // add tabs* to results for callback
  char *zErrMsg = 0;
  int rc = sqlite3_exec(db, query.c_str(), callback, (void*)&r, &zErrMsg);
  std::cout << r.header << std::endl << r.data << std::endl;
  return rc;
}

// run SQL statement, return sum of all values returned
ulong sqlitedb::select_lsum(const std::string& query) {
  stopwatch_t  stopwatch;
  int          rc      = 0;
  ulong        retval  = 0;
  sqlite3_stmt *stmt   = 0;
  const char   *pzTest = NULL;
  showquery(query);
  if(db==0) die("Query on closed database");
  rc = sqlite3_prepare_v2(db, query.c_str(), strlen(query.c_str()), &stmt, &pzTest);
  if(rc!=SQLITE_OK) die("SQL prepare error: " + query);
  rc=sqlite3_step(stmt);
  while(rc==SQLITE_ROW) { 
    retval += sqlite3_column_int64(stmt, 0);
    rc=sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  stopwatch.lap();
  o_query << ", " << stopwatch.runtime() << std::endl;
  return retval;
}

// run SQL statement, return ulong value (select)
ulong sqlitedb::select_long(const std::string& query) {
  stopwatch_t  stopwatch;
  ulong        retval  = 0;
  sqlite3_stmt *stmt   = NULL;
  const char   *pzTest = NULL;
  showquery(query);
  if(sqlite3_prepare_v2(db, query.c_str(), query.length(), &stmt, &pzTest)!=SQLITE_OK)  die("SQL prepare error: " + query);
  if(sqlite3_step(stmt)==SQLITE_ROW) { 
    retval = sqlite3_column_int64(stmt, 0);
    if(sqlite3_step(stmt)!=SQLITE_DONE) die("SQL: Multiple return values for " + query);
  }
  sqlite3_finalize(stmt);
  stopwatch.lap();
  o_query << ", " << stopwatch.runtime() << std::endl;
  return retval;
}

// run SQL statement, return char* (select). Returns NULL if no rows.
const std::string sqlitedb::select_text(const std::string& query) {
  std::string retval;
  const unsigned char * text;
  const  char  *pzTest = NULL;
  sqlite3_stmt *stmt   = NULL;
  showquery(query);
  if(sqlite3_prepare_v2(db, query.c_str(), query.length(), &stmt, &pzTest)!=SQLITE_OK)  die("SQL prepare error: " + query);
  if(sqlite3_step(stmt)==SQLITE_ROW) { 
    text = sqlite3_column_text(stmt, 0);
    retval = (char *)text;
    if(SQLITE_DONE!=sqlite3_step(stmt)) die("SQL: Multiple return values for " + query);
  }
  sqlite3_finalize(stmt);
  return retval;
}

/*******************************************************************************
 * Complex queries
 ******************************************************************************/

// Import data from another qdda database
void sqlitedb::import() {
  sql(R"(insert or replace into main.kv
select tmpdb.kv.k
, coalesce(main.kv.v,0) + tmpdb.kv.v
, tmpdb.kv.b from tmpdb.kv
left outer join main.kv on main.kv.k = tmpdb.kv.k
group by tmpdb.kv.k
order by main.kv._rowid_,tmpdb.kv.k)");
}

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

void sqlitedb::schema() {
  sql(R"(pragma page_size = 8192;
CREATE TABLE IF NOT EXISTS metadata(lock char(1) not null default 1, blksz integer, constraint pk_t1 primary key(lock), constraint ck_t1_l check (lock=1));
CREATE TABLE IF NOT EXISTS files(id integer primary key autoincrement, name TEXT, blocks integer, size integer);
CREATE TABLE IF NOT EXISTS buckets(type text,min integer, max integer,primary key (type,min));
CREATE TABLE IF NOT EXISTS bucketdesc(type text primary key,desc text);
CREATE TABLE IF NOT EXISTS kv(k unsigned integer primary key, v integer, b integer))");
}

// merge staging data into main table (performance optimized)
void sqlitedb::merge() {
  sql(R"(insert or replace into kv
select tmpdb.staging.k, coalesce(kv.v,0)+count(tmpdb.staging.k), tmpdb.staging.b from tmpdb.staging
left outer join kv on kv.k = tmpdb.staging.k
group by tmpdb.staging.k)");
}

/*******************************************************************************
 * Functions
 ******************************************************************************/

void sqlitedb::vacuum()    { sql("vacuum"); }
void sqlitedb::begin()     { sql("begin transaction"); }
void sqlitedb::end()       { sql("end transaction"); }
void sqlitedb::commit()    { end() ; begin(); }

ulong sqlitedb::blocksize()  { return select_long("select blksz from metadata"); }   // blocksize from database
ulong sqlitedb::rows()       { return select_long("select count(*) from kv"); }      // rows in kv table
ulong sqlitedb::tmprows()    { return select_long("select count(*) from staging"); } // rows in tmpdb.staging table

int sqlitedb::insert(ulong p1, ulong p2)                            { return stmt_insert.exec(p1,p2); }    // insert hash, compressed bytes
int sqlitedb::insbucket(const char * a,ulong p1, ulong p2)          { return stmt_bucket.exec(a,p1,p2); }  // insert a bucket value
int sqlitedb::savemeta(const char *name, ulong blocks, ulong bytes) { return stmt_meta.exec(name,blocks,bytes); } // save scanned stream metadata

// count required blocks after applying bucket compression
ulong sqlitedb::compressed(const char * t) {
  if(strlen(t)==0) return rows();
  return stmt_buckets.execsum(t) ; 
}

