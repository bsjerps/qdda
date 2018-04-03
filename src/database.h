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
#include "contrib/sqlite3.h"


//#define string std::string
typedef std::string string;

/*******************************************************************************
 * functions
 ******************************************************************************/
string parseFileName(string& in);  // prevent illegal db filenames

int fileIsSqlite3(const char * fn);          // test if file is SQLite3 format
int fileIsSqlite3(const string& s);     // test if file is SQLite3 format

int fileDeleteSqlite3(const string& s); // same for string type
int fileDeleteSqlite3(const char * fn);      // delete only if file is SQLite3

/*******************************************************************************
 * Structs
 ******************************************************************************/

/*******************************************************************************
 * Classes
 ******************************************************************************/

// a class to hold a prepared SQLite query. Automatically finalized by destructor
// Exec Can be called with one or 2 ulongs and return an ulong in case of
// select statement

class Query {
public:
  Query(sqlite3* db,const char *);
  ~Query();
  
  void  finalize();
  void  print(std::ostream& os);
  const char *sql();
  int   bind(const ulong);
  int   bind(const char*);
  int   bind(const string&);
  void  exec();
  ulong execl();
  ulong execl(ulong);
  ulong execl(ulong,ulong);
  const std::string exectext();
  void  report(const string& tabs);
private:
  void  step();
  void  reset();
  int   prep(sqlite3* db, const char* query);
  int ref; // param refcount
  sqlite3_stmt *pStmt;
};

std::ostream& operator<<(std::ostream& stream, Query& statement);

// Class to hold the structure of the SQLite database and various statements
class Database {
public:
  Database(const string& fn);
  ~Database();
  const char*  filename();
  int          unlinkdb();
  static int   createdb(const string& fn, const char* schema);
  static int   deletedb(const string& fn);
  static int   exists(const char* fn);
  void         settmpdir (const string& d) { tmpdir = d; };
  int          close();
  // various
  // ulong        getlong(const string&);
  // prefab queries
  void         vacuum();
protected:
  void         sql(const string& query);
  sqlite3* db;           // global sqlite database
  string   tmpdir;       // SQLITE_TMPDIR
};

class StagingDB: public Database {
public:
  StagingDB(const string& fn);
  static void  createdb(const string& fn, ulong blocksize);
  ulong        getblocksize();
  ulong        getrows();
  void         setblocksize(ulong);
  int          fillrandom(ulong rows, int blocksize, int dup);
  int          fillzero(ulong rows);
  void         insertkv(ulong, ulong);
  int          savemeta(const string name, ulong blocks, ulong bytes);
  void         begin();
  void         end();
private:
  Query q_setblocksize;
  Query q_getblocksize;
  Query q_getrows;
  Query q_insert;
  Query q_meta;
  Query q_fillrand;
  Query q_fillzero;
  Query q_begin;
  Query q_end;
};

class QddaDB: public Database {
public:
  QddaDB(const string& fn);
  static void  createdb(const string& fn);
  void         loadbuckets(const string&);
  void         import(const string&);
  void         merge(const string&);
  int          insbucket(const char *,ulong, ulong);
  void         set_comp_method();
  void         setmetadata(string);

  ulong        getallocated();
  ulong        getzero();
  ulong        gettotal();
  ulong        getused(ulong min=0);
  ulong        getunique();
  ulong        getnuniq();
  ulong        getdeduped();
  ulong        getbytescompressedraw();
  ulong        getbytescompressednet();
  ulong        getblocksize();
  const string getarrayid();
  ulong        gettmpblocksize();
  ulong        getrows();
  ulong        gettmprows();
  void         copymeta();
  void         report_files(const string& tabs);
  void         report_dedupe(const string& tabs);
  void         report_compress(const string& tabs);
private:
  // prepared queries
  Query q_getblocksize;
  Query q_getrows;
  Query q_getallocated;
  Query q_getarrayid;
  
  Query q_getzero;
  Query q_gettotal;
  Query q_getused;
  Query q_getunique;
  Query q_getnuniq;
  Query q_getdeduped;

  Query q_getbytescompressedraw;
  Query q_getbytescompressednet;
  
  Query q_attach;
  Query q_detach;
  Query q_loadbuckets;
  Query q_truncbuckets;

  Query q_hist_compress;
  Query q_listfiles;
  Query q_trunc_sums_compr;
  Query q_trunc_sums_deduped;
  Query q_update_sums_compr;
  Query q_update_sums_deduped;
  Query q_hist_dedupe;
};

#undef string

