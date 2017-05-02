/*******************************************************************************
 * Title       : qdda - quick & dirty dedup analyzer
 * Description : Checks files or block devices for duplicate blocks
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * More info   : http://outrun.nl/wiki/qdda
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
#include <zlib.h>
#include <lz4.h>

/*******************************************************************************
 * structs & classes
 ******************************************************************************/
class sqlitedb;

// a class to hold a prepared SQLite query. Automatically finalized by destructor
// Exec Can be called with one or 2 u_longs and return an u_long in case of
// select statement

extern const char * dbpath;

class sqlquery {
  sqlite3_stmt * stmt;
public:
  sqlquery(sqlitedb& db, const char * query);
  ~sqlquery();
  const char * show()   { return sqlite3_sql(stmt); }
  u_long get(int index) { return sqlite3_column_int64(stmt, index); };
  int next();
  u_long exec(const u_long p1);
  u_long exec(const u_long p1, const u_long p2);
  u_long exec(const u_long p1, const u_long p2, const u_long p3);
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
  static int   callback(void *r, int argc, char **argv, char **azColName);
public:
  friend class sqlquery;
  sqlitedb()   { fn = dbpath; db = 0 ; }
  ~sqlitedb()  { close() ; }
  void         filename(const std::string& f) { fn = f; };
  std::string& filename()                     {return fn; };
  int          open();
  void         close();
  void         checkpoint (void);
  u_long       select_long(const std::string& query);
  int          runquery(const char *,const std::string&);
  void         sql(const std::string& query);
};

// Holds a block count, easier for stats calculation
class blockval {
  static u_long blocksz;
  u_long        blks;  
public:
  void setblocksize(u_int b);
  blockval(u_long in) { blks = in; }
  blockval()          { blks = 0; }
  operator u_long() const { return blks; }
  void operator=(u_long b);
  u_long bytes()      { return blocksz * blks; };
  u_long blocksize()  { return blocksz; };
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

// long integer division but round up instead of round down
u_long divup(u_long x, u_long y) { return (x%y) ? x/y+1 : x/y; }

// return MiB for given # of blocks and blocksize. Round up.
u_long blocks2mib(ulong blocks, ulong blocksize) { return divup(blocks,(1024*1024 / blocksize)); }

// safe divide (just returns 0 if div by zero)
float safe_div(float x, float y) { return (y==0) ? 0 : x / y; }

// Print to string with precision N
template <typename T> std::string to_string(const T a_value, const int n = 2) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(n) << a_value;
    return out.str();
}

// Exit with error message
void die(std::string errmsg,int rc=10) { 
  std::cerr << "Error: " << errmsg << std::endl;
  exit(rc);
}
