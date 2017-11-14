/*******************************************************************************
 * Title       : qdda.h
 * Description : header file for qdda
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : http://outrun.nl/wiki/qdda
 ******************************************************************************/

#pragma once

/*******************************************************************************
 * functions
 ******************************************************************************/

int file_sqlite3(const char * fn);          // test if file is SQLite3 format
int sqlite_delete(const char * fn);         // delete only if file is SQLite3
std::string parsefilename(std::string& in); // prevent illegal db filenames
ulong hash6_md5(const char * buf, const int size);

/*******************************************************************************
 * structs & classes
 ******************************************************************************/

// Non-default menu actions 
struct MenuOpt {
  enum type {
    none, import, vacuum, test, xrep, version
  };
};


// Holds a block count, easier for stats calculation
class block_t {
  static ulong blocksize;
  ulong        blocks;  
public:
  static void setblocksize(u_int b) { blocksize = b; }
  block_t(ulong in)                { blocks = in; }
  block_t()                        { blocks = 0; }
  void operator=(ulong b)          { blocks = b; }
  operator ulong() const           { return blocks; }
  ulong bytes()                    { return blocksize * blocks; };
  std::string mib();
  std::string show();
};

class ratio_t {
  float p;
public:
  ratio_t(float in) { p = in ; }
  operator float() const { return p; }
  std::string show();
  std::string showperc();
  std::string showpercf();
};

// query result for SQLite3 callback function
struct qresult {
  const int *tabs;    // string with tab stops, comma separated
  std::string header; // callback places the header here
  std::string data;   // callback places the query result rows here
};

// a class to hold a prepared SQLite query. Automatically finalized by destructor
// Exec Can be called with one or 2 ulongs and return an ulong in case of
// select statement

class statement {
  sqlite3_stmt *stmt;
public:
  statement();
  ~statement();
  int prepare(sqlite3* db, const char* query);
  void finalize();
  const char *sql() { return sqlite3_sql(stmt); }
  ulong exec(const ulong p1);
  ulong exec(const ulong p1, const ulong p2);
  ulong exec(const char* p1, const ulong p2, const ulong p3);
  ulong execsum(const char * p1);
  ulong execsum(const char * p1,ulong p2,ulong p3);
};

// Class to hold the structure of the SQLite database and various statements
class sqlitedb {
  sqlite3      *db;       // global sqlite database
  std::string  tmpfn;     // tmpdb filename
  std::string  tmpdir;    // SQLITE_TMPDIR
  int          attached;  // 1 if tmpdb attached
  void schema();          // create the schema
  // prepared queries
  statement stmt_insert;
  statement stmt_meta;
  statement stmt_bucket;
  statement stmt_buckets;
  // callback function for dynamic sql queries
  static int   callback(void *r, int argc, char **argv, char **azColName);
public:
  sqlitedb();
  ~sqlitedb();
  // const char * filename() { return sqlite3_db_filename(db, "main"); }
  const char * tmpfilename();
  void         tempdir (const std::string& d) { tmpdir = d; };
  int          attach();
  void         detach(bool d=false); // detach staging/temp db (d=true: delete as well)
  int          open(const std::string& fn,ulong blocksize=16384);
  // various
  void         checkpoint (void);
  const std::string select_text(const std::string&);
  ulong        select_long(const std::string&);
  ulong        select_lsum(const std::string&);
  ulong        blocksize();
  ulong        rows();
  ulong        tmprows();
  ulong        compressed(const char *);
  int          runquery(const char *,const std::string&);
  void         sql(const std::string& query);
  // prefab queries
  void         loadbuckets();
  void         commit();
  void         import();
  void         merge();
  void         vacuum();
  void         begin();
  void         end();
  int          insert(ulong, ulong);
  int          insbucket(const char *,ulong, ulong);
  int          savemeta(const char *name, ulong blocks, ulong bytes);
};

