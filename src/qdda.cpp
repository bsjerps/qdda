/*******************************************************************************
 * Title       : qdda - quick & dirty dedup analyzer
 * Description : Checks files or block devices for duplicate blocks
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+
 * Disclaimer  : See https://www.gnu.org/licenses/gpl-3.0.txt
 * More info   : http://outrun.nl/wiki/qdda
 * -----------------------------------------------------------------------------
 * Revision history:
 * 1.0   - First published version
 * 1.2.1 - changed mhash crc32 to zlib crc32 (get rid of mhash lib dependency)
 *         changed zlib to lz4 compression (much better performance)
 *         now analyze all blocks for compression
 *         added thin ratio and other output changes
 * 1.3.0 - added All flash array aware compression (buckets), variable blocksize
 * 1.4.1 - Various improvements
 * 1.5.1 - Major performance increase when processing large amounts of data
 * 1.6.0 - Import function, file list, purge always after scanning, histograms
 * 1.7.0 - Change to MD5, performance improvements, tmpdir, improved reports
 * 1.7.1 - Minor bugfixes, improved long report, improved progress indicator,
 *         -a (append) option replaces -k (keep)
 * 1.7.2 - Fix rounding error, minor output changes
 * 1.7.3 - Replace openssl with compiled-in MD5 function (no dependency 
 *         on ssl libs). Increased max blocksize to 128K
 * 1.8.0 - Updated compression support, default blocksize now 16K
 * 1.8.2 - Bugfixes, updated reporting & documentation
 * 1.9.0 - Added 128K compression support, split primary and staging DB, 
 *         code cleanup, minor bugfixes, experimental VMAX compression
 * 2.0.0 - Multithreading and rewrite
 * 2.0.1 - Bugfix max filesize
 * 2.0.2 - Dynamic version strings
 * ---------------------------------------------------------------------------
 * Build notes: Requires lz4 >= 1.7.1
 ******************************************************************************/

#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>

#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>     /* srand, rand */

#include "lz4/lz4.h"
#include "tools.h"
#include "database.h"
#include "qdda.h"

extern "C" {
#include "md5/md5.h"
}

using namespace std;


/*******************************************************************************
 * global parameters - modify at own discretion
 ******************************************************************************/

#if defined(VERSION)
const char* PROGVERSION = TOSTRING(VERSION) RELEASE;
#else
const char* PROGVERSION = "0.0.1";
#endif

const char* kdefault_array = "x2";
const int kdefault_bandwidth = 200;
const int kmax_reader_threads = 32;

/*******************************************************************************
 * Initialization - globals
 ******************************************************************************/

bool g_debug = false; // global debug flag
bool g_query = false; // global query flag
bool g_quiet = false; // global quiet flag
bool g_abort = false; // global signal flag - true if ctrl-c was pressed (sigterm)
ulong starttime = epoch(); // start time of program

ofstream c_debug; // Debug stream

FileData::FileData(const string& file) {
  ratio=0; limit_mb=0;
  stringstream ss(file);
  string strlimit,strrepeat;

  getline(ss,filename,':');
  getline(ss,strlimit,',');
  getline(ss,strrepeat);
  
  if(filename=="compress") ratio=1;
  if(filename=="compress") { filename = "/dev/urandom"; limit_mb=1024; }
  if(filename=="random")   { filename = "/dev/urandom"; limit_mb=1024; }
  if(filename=="zero")     { filename = "/dev/zero";    limit_mb=1024; }
  
  if(!strlimit.empty()) limit_mb = atoll(strlimit.c_str());
  repeat   = atoi(strrepeat.c_str());

  ifs = new ifstream;
  c_debug << "Opening: " << file << endl;
  ifs->open(filename);
  if (!ifs->is_open()) {
    string msg = "File open failed, try:\nsudo setfacl -m u:";
    msg += getenv ("USER");
    msg += ":r ";
    msg += filename;
    msg += "\n";
    die(msg);
  }
}

/*******************************************************************************
 * Usage (from external files)
 ******************************************************************************/

const char * version_info  = 
  "Copyright (C) 2018 Bart Sjerps <bart@outrun.nl>\n"
  "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n"
  "This is free software: you are free to change and redistribute it.\n"
  "There is NO WARRANTY, to the extent permitted by law.\n\n"
  "build date: " __DATE__  "\nbuild time: " __TIME__ "\n";


const char * title_info = " - The Quick & Dirty Dedupe Analyzer\n"
  "Use for educational purposes only - actual array reduction results may vary\n";

extern const char* manpage_head;
extern const char* manpage_body;

void showtitle()   { if(!g_quiet) cout << "qdda " << PROGVERSION << title_info ; }
void showversion() { showtitle(); std::cout << version_info << std::endl; exit(0); }

/*******************************************************************************
 * Various
 ******************************************************************************/

/*******************************************************************************
 * Notes on hash algorithm
 * SQLite integer (also used for primary key on kv table) has max 8 bytes and is
 * signed integer. MAX value is 9 223 372 036 854 775 808. The max value of
 * unsigned long (64 bit) is 18446744073709551615UL which result in negative
 * hash values in the kv table. MD5 sum is 128 bit which would not fit in SQLite
 * integers and would be converted to another datatype (TXT or blob).
 * I found hash collisions with CRC32 on large datasets so needed another hash
 * function - see https://en.wikipedia.org/wiki/Birthday_problem#Probability_table
 * which shows 50% chance of collisions with 77000 rows on 32bit hash which equals
 * 630MiB using 8K blocksize. Problem gets worse when increasing the dataset.
 * 7 bytes (56 bits) is a tradeoff between DB space consumed, performance and
 * accuracy and has a 50% chance of collision with 316M rows (4832GB@16K)
 * which is fine for datasets up to many terabytes.
 * A 64-bit hash would get roughly 1 collision every 77TB@16K.
 ******************************************************************************/

// returns the least significant 7 bytes of the md5 hash (16 bytes) as unsigned long
uint64_t hash_md5(const char * src, char* zerobuf, const int size) {
  unsigned char digest[16];
  memset(zerobuf,0,size);      // initialize buf with zeroes
  if(memcmp (src,zerobuf,size)==0) return 0;         // return 0 for zero block
  MD5_CTX ctx;  
  MD5_Init(&ctx);
  MD5_Update(&ctx, src, size);
  MD5_Final(digest, &ctx);
  return                                // ignore chars 0-8
    ((uint64_t)(digest[8]&0X0F)  << 56) +  // pick 4 bits from byte 7
    ((uint64_t)digest[9]  << 48) +         // all bits from byte 6 to 0
    ((uint64_t)digest[10] << 40) +         // convert char* to ulong but keeping
    ((uint64_t)digest[11] << 32) +         // the right order, only use lower 6 bytes (char 10-15)
    ((uint64_t)digest[12] << 24) +         // SQLite integer is 8 byte signed so we need to stay within
    ((uint64_t)digest[13] << 16) +         // 8 bytes and unsigned. 6 bytes is best compromise
    ((uint64_t)digest[14] << 8 ) +
    ((uint64_t)digest[15]);
}

// Get compressed bytes for a compressed block - lz4
u_int compress(const char * src, char* buf, const int size) {
  int result = LZ4_compress_default(src, buf, size, size); // call LZ4 compression lib, only use bytecount & ignore data
  if(result>size) return size;                             // don't compress if size is larger than blocksize
  if(result==0) return size;
  return result;
}

/*******************************************************************************
 * Formatting & printing
 ******************************************************************************/

// Show scan speed info
void showprogress(const std::string& str) {
  if(g_quiet) return;
  static unsigned int l = 0;
  l = str.length() > l ? str.length() : l;     // track largest string length between calls
  if(str.length() == 0) {                      // clear line if string empty
    for(u_int i=0;i<l;i++) cout << ' ';   // overwrite old line with spaces
    for(u_int i=0;i<l;i++) cout << '\b';  // carriage return (no newline), \b = backspace
  } else {
    cout << str;
    for(u_int i=0;i<str.length();i++) cout << '\b'; // returns the cursor to original offset
    cout << std::flush;                             // print string ; carriage return
  }
}

// Show progress information, updated every N blocks. 
void progress(ulong blocks,ulong blocksize, ulong bytes, const char * msg) {
  static Stopwatch stopwatch;     // time of start processing file
  static Stopwatch prev;          // time of previous call
  stopwatch.lap();                // get current time interval
  static int   filenum   = 0;     // previous file num (tells us when new file process starts)
  static ulong prevbytes = 0;     // keep track of previous byte count
  if(filenum==0) {                // reset stats before processing new file
    stopwatch.reset();
    prev=stopwatch;
    prevbytes=bytes;
    filenum++;
  }
  auto avgsvctm = stopwatch;                               // service time of xx bytes since start of file
  auto cursvctm = stopwatch - prev;                        // service time of xx bytes since previous call
  auto avgbw = safeDiv_ulong(bytes,avgsvctm);              // bytes per second since start of file
  auto curbw = safeDiv_ulong((bytes-prevbytes),cursvctm);  // bytes per second since previous call
  stringstream ss;                                         // generate a string with the progress message
  ss << blocks        << " "                               // blocks scanned
     << blocksize     << "k blocks ("                      // blocksize
     << bytes/1048576 << " MiB) processed, "               // processed megabytes
     << curbw << "/"  << avgbw << " MB/s (cur/avg)";       // current/average bandwidth
  if(msg) ss << msg;                                       // add message if specified
  ss << "                 " ;                              // blank rest of line
  showprogress(ss.str());
  prev=stopwatch;  // save stopwatch time for next call
  prevbytes=bytes; // save byte count for next call
}

/*******************************************************************************
 * Functions
 ******************************************************************************/


void import(QddaDB& db, const string& filename) {
  if(!fileIsSqlite3(filename)) return;
  QddaDB idb(filename);
  ulong blocksize = db.blocksize;
  if(blocksize != idb.blocksize) die ("Incompatible blocksize");

  cout << "Adding " << idb.rows << " blocks from " << filename << " to " << db.rows << " existing blocks" << endl;
  db.import(filename);
}

// Merge staging data into kv table, track & display time to merge
void merge(QddaDB& db, Parameters& parameters) {
  if(!fileIsSqlite3(parameters.stagingname)) return;
  
  StagingDB sdb(parameters.stagingname);

  //stringstream ss2;
  ulong blocksize    = db.blocksize;
  ulong dbrows       = db.rows;
  ulong tmprows      = sdb.rows;
  ulong mib_staging  = tmprows*blocksize/1024;
  ulong mib_database = dbrows*blocksize/1024;
  ulong fsize1       = db.filesize();
  ulong fsize2       = sdb.filesize();

  if(blocksize != sdb.blocksize) die ("Incompatible blocksize");
  
  sdb.close();

  Stopwatch stopwatch;
  if(tmprows) { // do nothing if merge db has no rows
    if(!g_quiet) cout 
      << "Merging " << tmprows << " blocks (" 
      << mib_staging << " MiB) with " 
      << dbrows << " blocks (" 
      << mib_database << " MiB)" << flush;

    stopwatch.reset();
    ulong index_rps = tmprows*1000000/stopwatch;
    ulong index_mbps = mib_staging*1000000/stopwatch;
    
    stopwatch.reset();
    db.merge(parameters.stagingname);
    stopwatch.lap();
    
    auto time_merge = stopwatch;
    ulong merge_rps = (tmprows+dbrows)*1000000/time_merge;
    ulong merge_mbps = (mib_staging+mib_database)*1000000/time_merge;
    if(!g_quiet) cout << " in "
      << stopwatch.seconds() << " sec (" 
      << merge_rps << " blocks/s, " 
      << merge_mbps << " MiB/s)" << endl;
  }
  Database::deletedb(parameters.stagingname);
}

// test hashing, compression and insert performance
void cputest(QddaDB& db, Parameters& p) {
 
  StagingDB::createdb(p.stagingname,db.blocksize);
  StagingDB stagingdb(p.stagingname);
  
  const ulong mib       = 1024; // size of test set
  const ulong blocksize = db.blocksize;

  const ulong rows      = mib * 1024 / blocksize;
  const ulong bufsize   = mib * 1024 * 1024;
  char*       testdata  = new char[bufsize];
  ulong*      hashes    = new ulong[rows];
  ulong*      bytes     = new ulong[rows];
  ulong time_hash, time_compress, time_insert;
  char buf[blocksize*1024];
  
  Stopwatch   stopwatch;

  cout << fixed << setprecision(2) << "*** Synthetic performance test, 1 thread ***" << endl;

  cout << "Initializing:" << flush; 
  srand(1);
  memset(testdata,0,bufsize);
  for(ulong i=0;i<bufsize;i++) testdata[i] = (char)rand() % 8; // fill test buffer with random(ish) but compressible data
  cout << setw(15) << rows << " blocks, " << blocksize << "k (" << bufsize/1048576 << " MiB)" << endl;

  cout << "Hashing:     " << flush;
  stopwatch.reset();
  for(ulong i=0;i<rows;i++) hashes[i] = hash_md5(testdata + i*blocksize*1024,buf,blocksize*1024);
  time_hash = stopwatch.lap();
  
  cout << setw(15) << time_hash     << " usec, " 
       << setw(10) << (float)bufsize/time_hash << " MB/s, " 
       << setw(11) << float(rows)*1000000/time_hash << " rows/s"
       << endl;

  cout << "Compressing: " << flush;
  stopwatch.reset();
  
  for(ulong i=0;i<rows;i++) bytes[i] = compress(testdata + i*blocksize*1024,buf,blocksize*1024);
  time_compress = stopwatch.lap();
  cout << setw(15) << time_compress << " usec, " 
       << setw(10) << (float)bufsize/time_compress << " MB/s, " 
       << setw(11) << float(rows)*1000000/time_compress << " rows/s"
       << endl;

  // test sqlite insert performance
  cout << "DB insert:   " << flush;
  stopwatch.reset();

  stagingdb.begin();
  for(ulong i=0;i<rows;i++) stagingdb.insertdata(hashes[i],bytes[i]);
  stagingdb.end();
  time_insert = stopwatch.lap();
  cout << setw(15) << time_insert << " usec, "
       << setw(10) << (float)bufsize/time_insert   << " MB/s, "
       << setw(11) << float(rows)*1000000/time_insert << " rows/s"
       << endl;

  delete[] testdata;
  delete[] hashes;
  delete[] bytes;
  Database::deletedb(p.stagingname);
}


void manpage() {
  string cmd = "(";
  cmd += whoAmI();
  cmd += " --mandump > /tmp/qdda.1 ; man /tmp/qdda.1 ; rm /tmp/qdda.1 )";
  if(!system(cmd.c_str())) { };
}

const string& defaultDbName() {
  static string dbname;
  dbname = homeDir() + "/qdda.db";
  return dbname;
}

void showhelp(LongOptions& lo) {
  std::cout << "\nUsage: qdda <options> [FILE]...\nOptions:" << "\n";
  lo.printhelp(cout);
  std::cout << "\nMore info: qdda --man \nor the project homepage: http://outrun.nl/wiki/qdda\n\n";
}

void showlist() {
  showtitle();
  std::cout << "\narray options:\n\n"
    << "  --array x1    - XtremIO X1\n"
    << "  --array x2    - XtremIO X2\n"
    << "  --array vmax1 - VMAX All Flash (experimental)\n"
    << "  --array name=<name>,bs=<blocksize>,buckets=<bucketlist>\n\n"
    << "  blocksize in kb between 1 and 128, buckets in kb separated by +\n"
    << "  example: --array name=foo,bs=32,buckets=8+16+24+32\n"
    ;
}

void mandump(LongOptions& lo) {
  cout << manpage_head;
  lo.printman(cout);
  cout << manpage_body;
}

void findhash(Parameters& parameters) {
  StagingDB db(parameters.stagingname);
  db.findhash.bind(parameters.searchhash);
  db.findhash.report(cout,"20,20,10");
}

void tophash(QddaDB& db, int amount = 10) {
  db.tophash.bind(amount);
  db.tophash.report(cout,"20,10");
}

// safety guards against overwriting existing files or devices by SQLite
void ParseFileName(string& name) {
  char buf[160];
  if(!getcwd(buf, 160)) die("Get CWD failed");
  string cwd = buf;
  if(name.empty()) name = homeDir() + "/qdda.db";
  if(name[0] != '/') name = cwd + "/" + name;
  while (name.find("//") < string::npos) {
    auto i=name.find("//");
    name.replace(i,2,"/");
  }
  if (!name.compare(0,4,"/dev")  )            { die("/dev not allowed in filename: " + name);}
  if (!name.compare(0,5,"/proc") )            { die("/proc not allowed in filename: " + name);}
  if (!name.compare(0,4,"/sys")  )            { die("/sys not allowed in filename: " + name);}
  if (!name.find_last_of("/\\"))              { die("root dir not allowed: " + name);}
  if(name.find(".db")>name.length()) name += ".db";
}

string genStagingName(string& name) {
  string tmpname;
  tmpname = name.substr(0,name.find(".db"));
  tmpname += "-staging.db";
  return tmpname;
}

/*******************************************************************************
 * Main section - process options etc
 ******************************************************************************/

int main(int argc, char** argv) {
  int rc = 0;
  
  Parameters parameters = {};
  Parameters& p = parameters; // shorthand  

  v_FileData  filelist;

  // set default values
  p.workers   = cpuCount();
  p.readers   = kmax_reader_threads;
  p.bandwidth = kdefault_bandwidth;
  p.array     = kdefault_array;
  
  LongOptions opts;
  opts.add("version"  ,'V', ""            , showversion,  "show version and copyright info");
  opts.add("help"     ,'h', ""            , p.do_help,    "show usage");
  opts.add("man"      ,'m', ""            , manpage,      "show detailed manpage");
  opts.add("db"       ,'d', "<file>"      , p.dbname,     "database file path (default $HOME/qdda.db)");
  opts.add("append"   ,'a', ""            , p.append,     "Append data instead of deleting database");
  opts.add("delete"   , 0 , ""            , p.do_delete,  "Delete database");
  opts.add("quiet"    ,'q', ""            , g_quiet,      "Don't show progress indicator or intermediate results");
  opts.add("bandwidth",'b', "<mb/s>"      , p.bandwidth,  "Throttle bandwidth in MB/s (default 200, 0=disable)");
  opts.add("array"    , 0 , "<id|def>"    , p.array,      "set array type or custom definition <x1|x2|vmax1|definition>");
  opts.add("list"     ,'l', ""            , showlist,     "list supported array types and custom definition options");
  opts.add("detail"   ,'x', ""            , p.detail,     "Detailed report (file info and dedupe/compression histograms)");
  opts.add("dryrun"   ,'n', ""            , p.dryrun,     "skip staging db updates during scan");
  opts.add("purge"    , 0 , ""            , p.do_purge,   "Reclaim unused space in database (sqlite vacuum)");
  opts.add("import"   , 0 , "<file>"      , p.import,     "import another database (must have compatible metadata)");
  opts.add("cputest"  , 0 , ""            , p.do_cputest, "Single thread CPU performance test");
  opts.add("nomerge"  , 0 , ""            , p.skip,       "Skip staging data merge and reporting, keep staging database");
  opts.add("debug"    , 0 , ""            , g_debug,      "Enable debug output");
  opts.add("queries"  , 0 , ""            , g_query,      "Show SQLite queries and results"); // --show?
  opts.add("tmpdir"   , 0 , "<dir>"       , p.tmpdir,     "Set $SQLITE_TMPDIR for temporary files");
  opts.add("workers"  , 0 , "<wthreads>"  , p.workers,    "number of worker threads");
  opts.add("readers"  , 0 , "<rthreads>"  , p.readers,    "(max) number of reader threads");
  opts.add("findhash" , 0 , "<hash>"      , p.searchhash, "find blocks with hash=<hash> in staging db");
  opts.add("tophash"  , 0 , "<num>"       , p.tophash,    "show top <num> hashes by refcount");
  opts.add("mandump"  , 0 , ""            , p.do_mandump, "dump raw manpage to stdout");
#ifdef __DEBUG
  opts.add("buffers"  , 0 , "<buffers>"   , p.buffers,    "number of buffers (debug only!)");
#endif

  rc=opts.parse(argc,argv);
  if(rc) die ("Invalid option");

  if(p.do_help)         { showhelp(opts); exit(0); }
  else if(p.do_mandump) { mandump(opts); exit(0); }
  
  if(!p.tmpdir.empty()) setenv("SQLITE_TMPDIR",p.tmpdir.c_str(),1);

  showtitle();
  ParseFileName(p.dbname);
  p.stagingname = genStagingName(p.dbname);
  
  if(p.do_delete)  {
    if(!g_quiet) cout << "Deleting database " << p.dbname << endl;
    Database::deletedb(p.dbname); exit(0);
  }
  
  // Build filelist
  if(optind<argc || !isatty(fileno(stdin)) ) {
    if (!isatty(fileno(stdin)))
      filelist.push_back(FileData("/dev/stdin"));
    for (int i = optind; i < argc; ++i)
      filelist.push_back(FileData(argv[i]));
    if(!parameters.append) { // not appending -> delete old database
      if(!g_quiet) cout << "Creating new database " << p.dbname << endl;
      Database::deletedb(p.dbname);
      QddaDB::createdb(p.dbname);
    }
  }
  if(p.do_cputest && !p.append) {
    if(!g_quiet) cout << "Creating new database " << p.dbname << endl;
    Database::deletedb(p.dbname);
    QddaDB::createdb(p.dbname);
  }
  if(!Database::exists(p.dbname)) QddaDB::createdb(p.dbname);
  QddaDB db(p.dbname);
  db.parsemetadata(parameters.array);

  if(filelist.size()>0) 
    analyze(filelist, db, parameters);

  if(g_abort) return 1;

  if(p.do_purge)             { db.vacuum();           }
  else if(!p.import.empty()) { import(db,p.import);   }
  else if(p.do_cputest)      { cputest(db,p) ;        } 
  else if(p.searchhash!=0)   { findhash(p);           }
  else if(p.tophash!=0)      { tophash(db,p.tophash); }
  else {
    if(!parameters.skip)     { merge(db,parameters); }
    if(parameters.detail)    { reportDetail(db); }
    else if (!p.skip)        { report(db); }
  }
}

