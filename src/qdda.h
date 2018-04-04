/*******************************************************************************
 * Title       : qdda.h
 * Description : header file for qdda
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : http://outrun.nl/wiki/qdda
 ******************************************************************************/

//#include <fstream>
#include <vector>
#include "database.h"

#pragma once

/*******************************************************************************
 * functions
 ******************************************************************************/
 
class Filelist;
class Parameters;

ulong hash_md5(const char* src, char* zerobuf, const int size);
void  report(QddaDB& db);
void  reportHistograms(QddaDB& db);
u_int compress(const char * src,char * buf, const int size);
void  progress(ulong blocks,ulong blocksize, ulong bytes, const char * msg = NULL);
void  analyze(Filelist& filelist, QddaDB& db, Parameters& parameters);

/*******************************************************************************
 * structs & classes
 ******************************************************************************/

// Valid menu actions
//struct Parameters;

// Vectored list of open streams (files)
class Filelist {
public:
 ~Filelist();
  std::ifstream& operator[](uint);
  int            size();
  ulong          getBytes(int i);
  ulong          getBlocks(int i);
  const std::string& name(int i);
  void           open(const char *);
private:
  std::vector <std::ifstream *> ifs;
  std::vector <std::string> filename;
};


// Parameter set to pass between functions
struct Parameters {
  std::string tmpdir;
  std::string array;
  std::string import;
  int  bandwidth; // default bandwidth throttle (MB/s)
  int  workers;   // number of workers (threads)
  int  readers;   // max number of readers
  int  buffers;   // override read buffers
  bool do_help;
  bool do_mandump;
  bool do_cputest;
  bool do_dbtest;
  bool do_purge;
  bool do_delete;
  bool queries;
  bool skip;
  bool dryrun;
  bool append;
  bool quiet;
  bool detail;
};



