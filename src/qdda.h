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

ulong hash_md5(const char* src, char* zerobuf, const int size);
u_int compress(const char * src,char * buf, const int size);

//void analyze(Filelist& filelist, QddaDB& db, Parameters& parameters);
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
  int limit_mb;
};

/*
// Vectored list of open streams (files)
class Filelist {
public:
 ~Filelist();
  std::ifstream& operator[](uint);
  int            size();
  ulong          getBytes(int i);
  ulong          getBlocks(int i);
  const string&  name(int i);
  void           open(const char *);
private:
  std::vector <std::ifstream *> ifs;
  std::vector <string> filename;
  std::vector <int> limit_mb;
};*/

// Parameter set to pass between functions
struct Parameters {
  string dbname;
  string tmpdir;
  string array;
  string import;
  string dbtestopts;
  int  bandwidth; // default bandwidth throttle (MB/s)
  int  workers;   // number of workers (threads)
  int  readers;   // max number of readers
  int  buffers;   // override read buffers
  bool do_help;
  bool do_mandump;
  bool do_cputest;
  bool do_purge;
  bool do_create;
  bool do_delete;
  bool queries;
  bool skip;
  bool dryrun;
  bool append;
  bool quiet;
  bool detail;
};

#undef string


