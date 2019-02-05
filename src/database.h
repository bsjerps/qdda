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
#include "sqlite/sqlite3.h"

/*******************************************************************************
 * forward declarations
 ******************************************************************************/

struct sqlite3_stmt;
struct sqlite3;
class Database;

typedef sqlite3_int64 sql_int; // shorthand

/*******************************************************************************
 * Query class
 * hold a prepared SQLite query. Automatically finalized by destructor
 * provides a set of access and prepare functions to run the query
 ******************************************************************************/

class Query {
public:
  Query(sqlite3* db,const char *);     // create and prepare a query
  Query(Database& db,const char *);    // create and prepare a query
  ~Query();                            // call finalize
  void  print(std::ostream& os);       // print the expanded sql after running step()
  const char *sql();                   // return the query text
  int   bind(const sql_int);           // bind next parameter
  int   bind(const char*);             // same for char*
  int   bind(const std::string&);      // same for string
  int   bind();                        // bind NULL
  void  exec();                        // execute query, ignore results
  sql_int execi();                     // execute query, return sql int
  sql_int execi(sql_int p);            // same but bind parameter first
  sql_int execi(sql_int p,sql_int q);  // 2 parameters
  double execf();                      // execute query, return double
  const std::string execstr();         // return string result
  const char * sqlerror();             // show error message
  Query& operator<< (sql_int);         // bind operators, ostream style
  Query& operator<< (const char *);
  Query& operator<< (const std::string&);
  void  report(std::ostream& os, const IntArray& tabs); // run a query as report
private:
  void init(sqlite3* db, const char*); // shared constructor due to C++03
  Query(const Query&);                 // disable copy i.e. auto = (Query)
  int step();                          // execute query
  int reset();                         // reset for next execution
  int ref;                             // refcount for bind()ing parameters
  sqlite3_stmt *pStmt;
};

/*******************************************************************************
 * Database class - Parent class for SQLite databases
 * hold the structure of the SQLite database and various statements
 ******************************************************************************/

class Database {
public:
  explicit Database(const std::string& fn);
  ~Database();
  const char*  filename();
  size_t       filesize();
  int          unlinkdb();
  static int   createdb(const std::string& fn, const char* schema);
  static int   deletedb(const std::string& fn);
  static int   exists(const std::string& fn);
  static int   isValid(const char*);
  void         settmpdir(const std::string& d) { tmpdir = d; };
  int          attach(const std::string& s, const std::string& p);
  int          detach(const std::string& s);
  int          close();
  // various
  void         begin();
  void         end();
  void         vacuum();
  // ad-hoc select
  sql_int getint(const char *);           // get single int value from query
  double getfloat(const char *);          // get single float value from query
  const std::string getstr(const char *); // get string value from query
protected:
  void         sql(const std::string& query); // directly execute this query
  void         sql(const char* query);        // same for char*
  sqlite3*     db;           // global sqlite database
  std::string  tmpdir;       // SQLITE_TMPDIR
private:
  Database(const Database&) = delete;
  friend class Query;
};

/*******************************************************************************
 * StagingDB class - Staging database for qdda
 ******************************************************************************/

class StagingDB: public Database {
public:
  explicit StagingDB(const std::string& fn);
  static void createdb(const std::string& fn, int64 blocksize);
  int         fillrandom(sql_int rows, int blocksize, int dup);
  int         fillzero(sql_int rows);
  void        insertdata(uint64, uint64);
  int         insertmeta(const std::string& name, sql_int blocks, sql_int bytes);
  sql_int blocksize();
  sql_int getrows();
  void  setblocksize(sql_int);
  Query q_insert;
};

/*******************************************************************************
 * QddaDB class - Main database for qdda
 ******************************************************************************/

class QddaDB: public Database {
public:
  explicit QddaDB(const std::string& fn);
  static void  createdb(const std::string& fn);
  void  loadbuckets(const IntArray& buckets);
  void  import(const std::string&);
  void  merge(const std::string&);
  int   insbucket(const char *,int64, int64);
  void  set_comp_method();
  void  setmetadata(sql_int blocksz, sql_int method, sql_int interval, sql_int array, const IntArray& buckets);

  void  update();
  void  copymeta();
  void  squash();

  sql_int gettmpblocksize();
  sql_int gettmprows();
  sql_int getarrayid();
  sql_int getmethod();

  sql_int getinterval();
  sql_int getblocksize();
  sql_int getrows();
};

