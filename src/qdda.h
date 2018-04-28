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

// dirty hack to improve readability
#define string std::string

class FileData;

typedef std::vector<FileData> v_FileData;

/*******************************************************************************
 * functions
 ******************************************************************************/
 
class Filelist;
class Parameters;

uint64_t hash_md5(const char* src, char* zerobuf, const int size);
u_int compress(const char * src,char * buf, const int size);

void analyze(v_FileData& filelist, QddaDB& db, Parameters& parameters);

void report(QddaDB& db);
void reportDetail(QddaDB& db);

// show repeating progress line
void  progress(ulong blocks,ulong blocksize, ulong bytes, const char * msg = NULL);

/*******************************************************************************
 * structs & classes
 ******************************************************************************/

class FileData {
public:
  FileData(const string& name);
  std::ifstream* ifs;
  string filename;
  ulong limit_mb;
  int repeat;
  int ratio;
};

// Parameter set to pass between functions
struct Parameters {
  string dbname;
  string stagingname;
  string tmpdir;
  string array;
  string import;
  //string testopts;
  ulong searchhash;
  int  bandwidth; // default bandwidth throttle (MB/s)
  int  workers;   // number of workers (threads)
  int  readers;   // max number of readers
  int  buffers;   // override read buffers
  bool do_help;
  bool do_mandump;
  bool do_cputest;
  bool do_purge;
  bool do_delete;
  bool queries;
  bool skip;
  bool list;
  bool dryrun;
  bool append;
  bool quiet;
  bool detail;
  int tophash;
};

#undef string


