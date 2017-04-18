/*******************************************************************************
 * Title       : qdda - quick & dirty dedup analyzer
 * Description : Checks files or block devices for duplicate blocks
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * More info   : http://outrun.nl/wiki/qdda
 * -----------------------------------------------------------------------------
 * Revision history:
 * 1.0   - First published version
 * 1.2.1 - changed mhash crc32 to zlib crc32 (get rid of mhash lib dependency)
 *         changed zlib to lz4 compression (much better performance)
 *         now analyze all blocks for compression
 *         added thin ratio and other output changes
 * 1.3.0 - added All flash array aware compression (buckets), variable blocksize
 * ---------------------------------------------------------------------------
 * Build notes: Requires lz4 >= 1.7.1
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

#include "version.h" // version will be updated during rpm rebuild

/*******************************************************************************
 * global parameters - modify at own discretion
 ******************************************************************************/

const char * dbpath  = "/var/tmp/qdda.db"; // default database location

const u_long default_blksz  = 8192; // Default blocksize
const u_long default_bw     = 200;  // default bandwidth throttle (MiB/s)
const u_long blockspercycle = 64;   // read chunk blocks at a time when throttling
const u_long updateinterval = 5000; // progress report every N blocks

// column width for reporting
const int col1w = 15; // fits up to 9TB without messing up formatting
const int col2w = 10;

/*******************************************************************************
 * Constants - don't touch
 ******************************************************************************/

const long mebibyte      = 1024*1024; // Bytes per MiB
const long max_blocksize = 65536;     // max allowed blocksize

/*******************************************************************************
 * structs & classes
 ******************************************************************************/

class sqlitedb {
  sqlite3      *db;        // global sqlite database
  std::string fn;          // filename
  sqlite3_stmt *stmt_get;  // statement to get hash count
  sqlite3_stmt *stmt_ins;  // statement to insert new hash
  sqlite3_stmt *stmt_inc;  // statement to increase counter
public:
  sqlitedb();
  ~sqlitedb();
  void   filename(const std::string& f) { fn = f; };
  int    open();
  int    deletedb();
  int    db_exists();
  void   dumperr() { std::cerr << "SQL error: " << sqlite3_errmsg(db) << std::endl; }
  int    hashinsert (u_int hash, u_int bytes);
  int    hashincr   (u_int hash);
  u_long hashcount  (u_int hash);
  u_long select_long(const std::string& query);
  void   sql(const std::string& query);
};


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
 * SQL query text
 ******************************************************************************/

// Schema definition: // key-value store in SQLite: key = hash of block, v = block count, b=compressed bytes
// metadata keeps track of the blocksize - cannot chance blocksize once created. Force metadata to have one row only.
const char * sql_schema = R"(
CREATE TABLE IF NOT EXISTS kv(
k unsigned INTEGER PRIMARY KEY
, v unsigned integer
, b unsigned integer
);
CREATE TABLE IF NOT EXISTS metadata(
lock char(1) not null default 1
, blksz integer
, constraint pk_t1 primary key(lock)
, constraint ck_t1_l check (lock=1))
)";

// performance optimizations 
const char * sql_optimize = R"(
PRAGMA journal_mode = MEMORY;
PRAGMA synchronous = OFF;
)";

/*******************************************************************************
 * Macro's & inline functions
 ******************************************************************************/

// formatting

#define w1 setw(col1w)
#define w2 setw(col2w)

// Clock calc macros for readability
#define clocknow         std::chrono::high_resolution_clock::now()
#define clockdiff(t2,t1) std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count()

// long integer division but round up instead of round down
u_long divup(u_long x, u_long y) { return (x%y) ? x/y+1 : x/y; }

// return MiB for given # of blocks and blocksize. Round up.
u_long blocks2mib(ulong blocks, ulong blocksize) { return divup(blocks,(mebibyte / blocksize)); }

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
