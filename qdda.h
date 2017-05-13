/*******************************************************************************
 * Title       : qdda - quick & dirty dedup analyzer
 * Description : Checks files or block devices for duplicate blocks
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : http://outrun.nl/wiki/qdda
 ******************************************************************************/

#pragma once
#define debug      cout << "At line " << __LINE__ << endl;
#define dumpvar(v) std::cout << #v << "=" << v << std::endl;

#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cstring>
#include <chrono>

#include <sqlite3.h>
#include <unistd.h>
#include <lz4.h>
#include <openssl/md5.h>

/*******************************************************************************
 * notes
 ******************************************************************************/

// ULONG_MAX = 18446744073709551615UL (64 bit)
// UINT_MAX  = 4294967295U
// RAND_MAX  = 2147483647

/*******************************************************************************
 * structs & classes
 ******************************************************************************/
class sqlitedb;

// a class to hold a prepared SQLite query. Automatically finalized by destructor
// Exec Can be called with one or 2 ulongs and return an ulong in case of
// select statement

extern const char * dbpath;

class sqlquery {
  sqlite3_stmt * stmt;
public:
  sqlquery(sqlitedb& db, const char * query);
  ~sqlquery();
  const char* show()   { return sqlite3_sql(stmt); }
  ulong get(int index) { return sqlite3_column_int64(stmt, index); };
  int next();
  ulong exec(const ulong p1);
  ulong exec(const ulong p1, const ulong p2);
  ulong exec(const ulong p1, const ulong p2, const ulong p3);
};

// query result for SQLite3 callback function
struct qresult {
  const int *tabs;    // string with tab stops, comma separated
  std::string header; // callback places the header here
  std::string data;   // callback places the query result rows here
};

// Class to hold the structure of the SQLite database
class sqlitedb {
  sqlite3      *db;        // global sqlite database
  std::string  fn;         // filename
  std::string  tmpdir;     // SQLITE_TMPDIR
  static int   callback(void *r, int argc, char **argv, char **azColName);
public:
  friend class sqlquery;
  sqlitedb()                                  { fn = dbpath; db = 0 ; }
  ~sqlitedb()                                 { close() ; }
  void         filename(const std::string& f) { fn = f; };
  void         tempdir (const std::string& d) { tmpdir = d; };
  std::string& filename()                     { return fn; };
  int          open();
  void         close()                        { if(db) { sqlite3_close(db); db = 0; }}
  void         checkpoint (void);
  ulong        select_long(const std::string& query);
  int          runquery(const char *,const std::string&);
  void         sql(const std::string& query);
};

// Holds a block count, easier for stats calculation
class blockval {
  static ulong blocksz;
  ulong        blks;  
public:
  static void setblocksize(u_int b) { blocksz = b; }
  blockval(ulong in)                { blks = in; }
  blockval()                        { blks = 0; }
  operator ulong() const            { return blks; }
  void operator=(ulong b)           { blks = b; }
  ulong bytes()                     { return blocksz * blks; };
  ulong blocksize()                 { return blocksz; };
};

/*******************************************************************************
 * Macro's & inline functions
 ******************************************************************************/

// formatting output
#define w1 setw(col1w)
#define w2 setw(col2w)

// Clock calc macros for readability
#define clocknow         std::chrono::high_resolution_clock::now()
#define clockdiff(t2,t1) std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count()

// long random, between 0 and 18446744073709551615UL;
ulong lrand() { return ((ulong)rand() << 32) + rand() ; }

// long integer division but round up instead of round down
ulong divup(ulong x, ulong y) { return (x%y) ? x/y+1 : x/y; }

// return MiB for given # of blocks and blocksize. Round up.
ulong blocks2mib(ulong blocks, ulong blocksize) { return divup(blocks,(1024*1024 / blocksize)); }

// safe divide (just returns 0 if div by zero)
float safediv_float(float x, float y) { return (y==0) ? 0 : x / y; }
ulong safediv_ulong(ulong x, ulong y) { return (y==0) ? 0 : x / y; }

// Print to string with precision N
template <typename T> std::string to_string(const T a_value, const int n = 2) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(n) << a_value;
    return out.str();
}

// return required bits for an unsigned int value
unsigned bindepth(unsigned val) {
  int depth=0;
  while (val) { val>>=1, depth++; }
  return depth;
}

// Exit with error message
void die(std::string errmsg,int rc=10) { 
  std::cerr << "Error: " << errmsg << std::endl;
  exit(rc);
}
