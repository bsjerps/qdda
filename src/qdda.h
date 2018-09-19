/*******************************************************************************
 * Title       : qdda.h
 * Description : header file for qdda
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : http://outrun.nl/wiki/qdda
 ******************************************************************************/

#pragma once

#include <vector>
#include "database.h"

class FileData;

typedef std::vector<FileData> v_FileData;

/*******************************************************************************
 * functions
 ******************************************************************************/
 
class Filelist;
class Parameters;

uint64_t hash_md5(const char* src, char* zerobuf, const int size);

u_int compress_none(const char * src,char * buf, const int size);
u_int compress_lz4(const char * src,char * buf, const int size);
u_int compress_deflate(const char * src,char * buf, const int size);

void analyze(v_FileData& filelist, QddaDB& db, Parameters& parameters);

void report(QddaDB& db);
void reportDetail(QddaDB& db);

// show repeating progress line
void  progress(ulong blocks,ulong blocksize, ulong bytes, const char * msg = NULL);

/*******************************************************************************
 * structs & classes
 ******************************************************************************/

class Compression {
public:
  explicit Compression()              { method=none; level=0; }
  enum CompressMethod { none, lz4, deflate };
  void setMethod(const std::string&);
  void setMethod(const std::string&, int, int);
  void setInterval(int);
  int  getMethod()                    { return method; };
  int  getLevel()                     { return level; }
  int  getInterval()                  { return interval; }
  operator const char*()              { return namelist[method];}
  void print(std::ostream& os)        { os << namelist[method] ; }
private:
  static const char* namelist[8];
  CompressMethod method;
  int interval;
  int level;
};

class FileData {
public:
  explicit FileData(const std::string& name);
  std::ifstream* ifs;
  std::string    filename;
  ulong          limit_mb;
  int            repeat;
  int            ratio;
private:
};

// Command line options
struct Options {
  bool do_help;
  bool do_mandump;
  bool do_cputest;
  bool do_purge;
  bool do_delete;
  bool do_demo;
  bool do_update;
  bool append;
  std::string dbname,compress;
};

// Parameter set to pass between functions
struct Parameters {
  
  std::string stagingname;
  std::string tmpdir;
  std::string array;
  std::string import;

  std::string buckets;
  Compression compression;

  ulong searchhash;
  ulong blocksize;
  int  level;      // compression level
  int  interval;   // sample interval
  int  bandwidth;  // default bandwidth throttle (MB/s)
  int  workers;    // number of workers (threads)
  int  readers;    // max number of readers
  int  buffers;    // override read buffers

  bool queries;
  bool skip;
  bool list;
  bool dryrun;

  bool quiet;
  bool detail;
  bool squash;
  int tophash;
};

#undef string


