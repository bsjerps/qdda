/*******************************************************************************
 * Title       : qdda.h
 * Description : header file for qdda
 * Author      : Bart Sjerps <bart@dirty-cache.com>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : https://wiki.dirty-cache.com/qdda
 ******************************************************************************/

#pragma once

#include <vector>
#include "database.h"

class FileData;
class Parameters;

typedef std::vector<FileData> v_FileData;
typedef BoundedVal<int,1,128> Blocksize;
typedef BoundedVal<int,1,99>  Interval;

/*******************************************************************************
 * functions
 ******************************************************************************/

uint64_t hash_md5(const char* src, char* zerobuf, const int size);

u_int compress_none(const char * src,char * buf, const int size);
u_int compress_lz4(const char * src,char * buf, const int size);
u_int compress_deflate(const char * src,char * buf, const int size);

void analyze(v_FileData& filelist, QddaDB& db, Parameters& parameters);

void report(QddaDB& db);
void reportDetail(QddaDB& db);

// show repeating progress line
void  progress(int64 blocks,int64 blocksize, size_t bytes, const char * msg = NULL);

/*******************************************************************************
 * Metadata class - holds info about arrayL block size, compression algo, etc
 ******************************************************************************/

class Metadata {
public:
  enum Method { m_none, lz4, deflate };
  enum Array  { a_none, custom, x1, x2, vmax, pmax };

  Metadata();
  Method getMethod()     { return method; };
  int getBlocksize()     { return blocksize; };
  int getInterval()      { return interval; }
  int getArray()         { return array; }
  IntArray& getBuckets() { return buckets; }

  void setMethod(const std::string&);
  int setArray(const std::string&);
  void setBlocksize(int);
  void setInterval(int);

  static const char* getMethodName(int);
  static const char* getArrayName(int);

private:
  void init(int blksz, int interval, Array a, Method m);
  Array array;         // array id
  Blocksize blocksize; // blocksize in KiB
  Method method;       // compress method (algorithm)
  Interval interval;   // sample interval for compression
  IntArray buckets;    // array of bucket sizes
};

/*******************************************************************************
 * Filedata class - info about files/streams to be scanned
 ******************************************************************************/

class FileData {
public:
  explicit FileData(const std::string& name);
  std::ifstream* ifs;      // opened stream
  std::string    filename; // original file name
  int64          limit_mb; // Stop scanning after x MiB (testing) 0 = read to end
  int            repeat;   // Simulate multiple scans (demo/testing) normal = 1
  bool           ratio;    // Simulate compression ratio, default = 0
};

/*******************************************************************************
 * Options - command line options/switches
 ******************************************************************************/

struct Options {
  bool do_help;
  bool do_mandump;
  bool do_bashdump;
  bool do_cputest;
  bool do_purge;
  bool do_delete;
  bool do_update;

  bool squash;
  bool append;
  bool detail;

  int   tophash;
  int64 shash;
  std::string array;
  std::string dbname;
  std::string compress;
  std::string import;
};

/*******************************************************************************
 * Parameters - for easy sharing between functions
 ******************************************************************************/
struct Parameters {

  std::string stagingname;
  std::string tmpdir;

  int bandwidth; // default bandwidth throttle (MB/s)
  int workers;   // number of workers (threads)
  int readers;   // max number of readers
  int buffers;   // override read buffers

  bool queries;  // show sqlite queries 
  bool skip;     // skip merge, keep staging database
  bool dryrun;   // don't update staging database
};

