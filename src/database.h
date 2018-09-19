/*******************************************************************************
 * Title       : database.h
 * Description : header file for database.cpp
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : http://outrun.nl/wiki/qdda
 ******************************************************************************/

#pragma once

#include <string>
#include <limits.h>

/*******************************************************************************
 * functions
 ******************************************************************************/

int fileIsSqlite3(const char * fn);          // test if file is SQLite3 format
int fileIsSqlite3(const std::string& s);     // test if file is SQLite3 format

/*******************************************************************************
 * forward decs
 ******************************************************************************/

struct sqlite3_stmt;
struct sqlite3;
class Database;

/*******************************************************************************
 * Query class
 * hold a prepared SQLite query. Automatically finalized by destructor
 * provides a set of access and prepare functions to run the query
 ******************************************************************************/

class Query {
public:
  Query(sqlite3* db,const char *);  // create and prepare a query
  Query(Database& db,const char *); // create and prepare a query
  ~Query();                         // call finalize
  void  print(std::ostream& os);    // print the expanded sql after running step()
  const char *sql();                // return the query text
  int   bind(const ulong);          // bind next parameter
  int   bind(const char*);          // same for char*
  int   bind(const std::string&);   // same for string
  int   bind();                     // bind NULL
  void  exec();                     // execute query, ignore results
  ulong execl();                    // execute query, return ulong
  float execf();                    // execute query, return float
  ulong execl(ulong p);             // same but bind parameter first
  ulong execl(ulong p,ulong q);     // 2 parameters
  const std::string execstr();      // return string result
  const char * sqlerror();          // show error message
  void  report(std::ostream& os, const std::string& tabs); // run a query as report
  // bind operators
  Query& operator<< (ulong);
  Query& operator<< (uint32_t);
  Query& operator<< (const char *);
  Query& operator<< (const std::string&);
  
private:
  Query(const Query&);              // disable copy i.e. auto = (Query)
  int step();                       // execute query
  int reset();                      // reset for next execution
  int ref;                          // refcount for bind()ing parameters
  sqlite3_stmt *pStmt;
};

// Class to hold the structure of the SQLite database and various statements
class Database {
public:
  explicit Database(const std::string& fn);
  ~Database();
  const char*  filename();
  ulong        filesize();
  int          unlinkdb();
  static int   createdb(const std::string& fn, const char* schema);
  static int   deletedb(const std::string& fn);
  static int   exists(const std::string& fn);
  void         settmpdir (const std::string& d) { tmpdir = d; };
  int          attach(const std::string& s, const std::string& p);
  int          detach(const std::string& s);
  int          close();
  // various
  void         begin();
  void         end();
  void         vacuum();
  // ad-hoc select
  ulong getul(const char *); // get single ulong value from query
  float getfl(const char *); // get single float value from query
  const std::string getstr(const char *); // get string value from query
protected:
  void         sql(const std::string& query);
  void         sql(const char* query);
  sqlite3*     db;           // global sqlite database
  std::string  tmpdir;       // SQLITE_TMPDIR
private:
  Database(const Database&) = delete;
  friend class Query;
};

class StagingDB: public Database {
public:
  explicit StagingDB(const std::string& fn);
 ~StagingDB(); 
  static void createdb(const std::string& fn, ulong blocksize);
  int         fillrandom(ulong rows, int blocksize, int dup);
  int         fillzero(ulong rows);
  void        insertdata(ulong, ulong);
  int         insertmeta(const std::string& name, ulong blocks, ulong bytes);
  
  ulong blocksize();
  ulong getrows();
  void setblocksize(ulong);
  
  Query q_insert;
};

class QddaDB: public Database {
public:
  explicit QddaDB(const std::string& fn);
  static void  createdb(const std::string& fn);
  void         loadbuckets(const std::string&);
  void         import(const std::string&);
  void         merge(const std::string&);
  int          insbucket(const char *,ulong, ulong);
  void         set_comp_method();
  void         setmetadata(int blocksize, const char* compr, const char* name, const std::string& buckets);
  void         update();
  ulong        gettmpblocksize();
  ulong        gettmprows();
  const std::string getarrayid();
  void         copymeta();
  void         squash();
  
  ulong blocksize();
  ulong getrows();
};

