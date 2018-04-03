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
 * 2.0.0 - Multithreading
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
#include <lz4.h>
#include <pthread.h>
// #include <getopt.h>

#include <stdlib.h>     /* srand, rand */

#include "tools.h"
#include "database.h"
#include "qdda.h"

extern "C" {
#include "contrib/md5.h"
}

using namespace std;


/*******************************************************************************
 * global parameters - modify at own discretion
 ******************************************************************************/

const char* PROGVERSION   = "1.9.2B";
const char* DEFAULT_TMP    = "/var/tmp"; // tmpdir for SQLite temp files
const ulong DEFAULT_BANDWIDTH = 200;     //

/*******************************************************************************
 * Constants - don't touch
 ******************************************************************************/

const ulong MEBIBYTE          = 1048576; // Bytes per MiB

/*******************************************************************************
 * Initialization - globals
 ******************************************************************************/

bool g_debug = false; // global debug flag
bool g_query = false; // global query flag
bool g_quiet = false; // global quiet flag

ofstream c_debug;                             // Debug stream, open with -D option
// ofstream c_verbose("/dev/tty");               // Progress stream, disable with -q option
ulong    starttime = epoch();                 // start time of program

//Options options;
//ulong Block_t::blocksize = 0; // static initialization outside class

int Filelist::size() {   return ifs.size(); }

ifstream& Filelist::operator[](uint i) {
  if(i>size()) die("File index error");
  return *ifs[i];
};

const std::string& Filelist::name(int i) {
  if(i>size()) die("File index error");
  return filename[i];
}

Filelist::~Filelist() {
  for(int i = 0; i<size() ; i++)
    delete ifs[i];
}

void Filelist::open(const char * file) {
  ifs.push_back(new ifstream);
  filename.push_back(file);
  c_debug << "Opening: " << file << endl;
  ifs.back()->open(file);
  if (!ifs.back()->is_open()) {
    string msg = "File open failed (try: sudo setfacl -m u:";
    msg += getenv ("USER");
    msg += ":r ";
    msg += file;
    msg += ")";
    die(msg);
  }
}

/*******************************************************************************
 * Usage (from external files)
 ******************************************************************************/

const char * version_info  = 
	"Copyright (C) 2017 Bart Sjerps <bart@outrun.nl>\n"
	"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n"
	"This is free software: you are free to change and redistribute it.\n"
	"There is NO WARRANTY, to the extent permitted by law.\n\n"
	"build date: " __DATE__  "\nbuild time: " __TIME__ "\n";


const char * title_info = 
	" - The Quick & Dirty Dedupe Analyzer\n"
	"Use for educational purposes only - actual array reduction results may vary\n";

//extern const char* qdda_help;
extern const char* qdda_longhelp;
extern const char* manpage_head;
extern const char* manpage_body;

void showtitle()   { if(!g_quiet) cout << "qdda " << PROGVERSION << title_info ; }
void showversion() { std::cout << version_info << std::endl; exit(0); }
void longhelp()    { std::cout << qdda_longhelp << std::endl; }

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
ulong hash_md5(const char * src, char* zerobuf, const int size) {
  unsigned char digest[16];
  memset(zerobuf,0,size);      // initialize buf with zeroes
  if(memcmp (src,zerobuf,size)==0) return 0;         // return 0 for zero block
  MD5_CTX ctx;  
  MD5_Init(&ctx);
  MD5_Update(&ctx, src, size);
  MD5_Final(digest, &ctx);
  return                            // ignore chars 0-9
    ((ulong)(digest[8]&0X0F)  << 56) +
    ((ulong)digest[9]  << 48) +     // enable this for 56-bit hashes vs 48-bit
    ((ulong)digest[10] << 40) +     // convert char* to ulong but keeping
    ((ulong)digest[11] << 32) +     // the right order, only use lower 6 bytes (char 10-15)
    ((ulong)digest[12] << 24) +     // SQLite integer is 8 byte signed so we need to stay within
    ((ulong)digest[13] << 16) +     // 8 bytes and unsigned. 6 bytes is best compromise
    ((ulong)digest[14] << 8 ) +
    ((ulong)digest[15]);
}

// Get compressed bytes for a compressed block - lz4
u_int compress(const char * src, char* buf, const int size) {
  // memset(buf,0,MAX_BLKSIZE+1024);
  int result = LZ4_compress_default(src, buf, size, size); // call LZ4 compression lib, only use bytecount & ignore data
  if(result>size) return size;                                  // don't compress if size is larger than blocksize
  if(result==0) return size;
  return result;
}

/*******************************************************************************
 * Formatting & printing
 ******************************************************************************/
/*
// Dump the hash values
void showdump(ulong block, ulong hash,ulong bytes) {
  cout << dec << setw(9) << setfill('0') << block
       << ","  << hex << setw(18) << showbase << internal << hash 
       << ","  << dec << setw(5)  << bytes << endl;
}
*/
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
  auto avgsvctm = stopwatch;                                // service time of xx bytes since start of file
  auto cursvctm = stopwatch - prev;                         // service time of xx bytes since previous call
  auto avgbw = safeDiv_ulong(bytes,avgsvctm);               // bytes per second since start of file
  auto curbw = safeDiv_ulong((bytes-prevbytes),cursvctm);   // bytes per second since previous call
  stringstream ss;                                          // generate a string with the progress message
  ss << blocks         << " "                               // blocks scanned
     << blocksize      << "k blocks ("                      // blocksize
     << bytes/MEBIBYTE << " MiB) processed, "               // processed megabytes
     << curbw << "/" << avgbw << " MB/s (cur/avg)";         // current/average bandwidth
  if(msg) ss << msg;                                        // add message if specified
  ss << "                 " ;                               // blank rest of line
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
  auto blocksize = db.getblocksize();
  if(blocksize != idb.getblocksize()) die ("Incompatible blocksize");

  cout << "Adding " << idb.getrows() << " blocks from " << filename << " to " << db.getrows() << " existing blocks" << endl;
  db.import(filename);
}

// Merge staging data into kv table, track & display time to merge
void merge(QddaDB& db, Parameters& parameters) {
  string tmpname = parameters.tmpdir + "/qdda-staging.db";
  if(!fileIsSqlite3(tmpname)) return;
  
  StagingDB sdb(tmpname);

  stringstream ss1,ss2;
  auto blocksize     = db.getblocksize();
  auto dbrows        = db.getrows();
  auto tmprows       = sdb.getrows();
  auto mib_staging   = tmprows*blocksize/1024;
  auto mib_database  = dbrows*blocksize/1024;

  if(blocksize != sdb.getblocksize()) die ("Incompatible blocksize");
  
  sdb.close();

  Stopwatch stopwatch;
  if(tmprows) { // do nothing if merge db has no rows
    if(!g_quiet) cout << "Merging " << tmprows << " blocks (" << mib_staging << " MiB) with " << dbrows << " blocks (" << mib_database << " MiB)"; // << endl;
    showprogress(ss1.str());
    stopwatch.reset();
    ulong index_rps = tmprows*1000000/stopwatch;
    ulong index_mbps = mib_staging*1000000/stopwatch;
    // ss1 << " in " << stopwatch.seconds() << " sec (" << index_rps << " blocks/s, " << index_mbps << " MiB/s), ";
    ss2 << ss1.str() << ", Joining" << flush;
    showprogress(ss2.str());

    stopwatch.reset();
    db.merge(tmpname);
    stopwatch.lap();
    
    auto time_merge = stopwatch;
    ulong merge_rps = (tmprows+dbrows)*1000000/time_merge;
    ulong merge_mbps = (mib_staging+mib_database)*1000000/time_merge;
    ss2 << " in " << stopwatch.seconds() << " sec (" << merge_rps << " blocks/s, " << merge_mbps << " MiB/s)";
    if(!g_quiet) cout << ss2.str() << endl;
  }
  Database::deletedb(tmpname.c_str());
}


// test merging performance with random data, format rows:w0:w1:w2:...
// sizegb:zeroval:dup1:dup2:...
// totalsize,zero=xx,dup1=xx
void mergetest(QddaDB& db, Parameters& parameters, const string& p) {
  string tmpname = parameters.tmpdir + "/qdda-staging.db";
  StagingDB::createdb(tmpname,db.getblocksize());
  StagingDB stagingdb(tmpname);

  const ulong blocksize = db.getblocksize();
  const ulong rowspergb = square(1024) / blocksize;
  
  ulong        rows = 0;  
  string       str;
  stringstream ss(p);
  
  while(ss.good()) {
    getline(ss,str,',');
    int dup=0;
    ulong r=0;
    if(isNum(str)) {
      r=atoi(str.c_str());
      dup=1;
      rows+=r*rowspergb;
      stagingdb.fillrandom(rowspergb*r,blocksize,dup);
    } else if (str.substr(0,4) == "zero") {
      dup=0;
      r=atoi(str.substr(5).c_str());
      rows += r*rowspergb;
      stagingdb.fillzero(rowspergb*r);
    } else if (str.substr(0,3) == "dup") {
      dup=atoi(str.substr(3,1).c_str());
      r=atoi(str.substr(5).c_str());
      rows += r*rowspergb*dup;
      stagingdb.fillrandom(rowspergb*r,blocksize,dup);
    } else {
      cerr << "Cannot parse " << str << endl;
    }
  }

  Stopwatch stopwatch;
  cout << "Merge test: Loading " << rows << " " << blocksize << "k random blocks (" << rows*blocksize/1024 << " MiB) " << flush;

  stopwatch.lap();
  cout << "in " << stopwatch.seconds() << " s" << endl;
  stagingdb.savemeta("mergetest", rows, rows*blocksize*1024);
}

// test hashing, compression and insert performance
void speedtest(QddaDB& db) {
  
  const char* tmpname = "/var/tmp/staging-test.db";
  StagingDB::createdb(tmpname,db.getblocksize());
  StagingDB stagingdb(tmpname);
  
  const ulong gb        = 1; // size of test set
  const ulong blocksize = db.getblocksize();

  const ulong rows      = gb * 1024 * MEBIBYTE / (blocksize*1024);
  const ulong bufsize   = gb * 1024 * MEBIBYTE;
  char*       testdata  = new char[bufsize];
  ulong time_hash, time_compress, time_insert, time_total=0;
  char buf[blocksize*1024];
  
  Stopwatch   stopwatch;

  cout << fixed << setprecision(2) << "*** Synthetic performance test, 1 thread ***" << endl;

  cout << "Initializing:" << flush; 
  srand(1);
  memset(testdata,0,bufsize);
  for(ulong i=0;i<bufsize;i++) testdata[i] = (char)rand() % 256; // fill test buffer with random data
  cout << setw(15) << rows << " blocks, " << blocksize << "k (" << bufsize/MEBIBYTE << " MiB)" << endl;

  cout << "Hashing:     " << flush;
  stopwatch.reset();
  for(ulong i=0;i<rows;i++) hash_md5(testdata + i*blocksize*1024,buf,blocksize*1024);
  time_hash = stopwatch.lap(); time_total += time_hash;
  
  cout << setw(15) << time_hash     << " usec, " << setw(10) << (float)bufsize/time_hash     << " MB/s" << endl;

  cout << "Compressing: " << flush;
  stopwatch.reset();
  for(ulong i=0;i<rows;i++) compress(testdata + i*blocksize*1024,buf,blocksize*1024);
  time_compress = stopwatch.lap(); time_total += time_compress;
  cout << setw(15) << time_compress << " usec, " << setw(10) << (float)bufsize/time_compress << " MB/s" << endl;
  
  // test sqlite insert performance
  cout << "DB insert:   " << flush;
  stopwatch.reset();

  stagingdb.begin();
  for(ulong i=0;i<rows;i++) stagingdb.insertkv(i,blocksize);
  // for(ulong i=0;i<1000000;i++) stagingdb.insertkv(i,blocksize);
  stagingdb.end();
  time_insert = stopwatch.lap(); time_total += time_insert;
  cout << setw(15) << time_insert   << " usec, " << setw(10) << (float)bufsize/time_insert   << " MB/s" << endl;

  // time_total = time_hash + time_compress + time_insert;
  cout << "Total:       " << setw(15) << time_total    << " usec, " << setw(10) << (float)bufsize/time_total    << " MB/s" << endl;
  delete[] testdata;
  Database::deletedb(tmpname);
}

// parse arg and call either speedtest (arg=0) or mergetest
void perftest(QddaDB& db, Parameters& args, const string& p) {
  if(p != "0") {
    mergetest(db, args, p);
  } else {
    speedtest(db);
  }
}


void mandump() {
  //options.printman(cout);
  exit(0);
}

void manpage() {
  string cmd;
  //cmd = "( qddaman=$(mktemp) ; ";
  cmd = "(";
  cmd += whoAmI();
  cmd += " --mandump > /tmp/qdda.1 ; man /tmp/qdda.1 ; rm /tmp/qdda.1 )";
  int rc=system(cmd.c_str());
  exit(0);
}

const string& defaultDbName() {
  static string dbname;
  dbname = homeDir() + "/qdda.db";
  return dbname;
}

void foobar(Parameters&, const char * p) {
  cout << "Foobar!!!" << endl;
  exit(0);
}
void foobar2() {
  cout << "Foobar!!!" << endl;
  exit(0);
}

int experiment() {
  int rc = 0;
  cout << homeDir() << endl;
  dumpvar(__GNUC__);
  dumpvar(__GNUC_MINOR__);
  cout << fileSystemFree("/var/tmp") << endl;
  exit(rc);
}

void showh(LongOptions& lo) {
  std::cout << "\nUsage: qdda <options> [FILE]...\nOptions:" << "\n";
  lo.printhelp(cout);
}

void mandump(LongOptions& lo) {
  cout << manpage_head;
  lo.printman(cout);
  cout << manpage_body;
}

/*******************************************************************************
 * Main section - process options etc
 ******************************************************************************/

int main(int argc, char** argv) {
  int rc = 0;
  experiment();
  //menustuff(); exit(0);
  
  MenuOpt    action = MenuOpt::none;
  string     dbname    = defaultDbName(); // DEFAULT_DBNAME;
  Parameters parameters = {};
  LongOptions opts;
  Filelist   filelist;
  
  parameters.workers   = cpuCount(); // + 2;
  parameters.readers   = 32;
  parameters.bandwidth = DEFAULT_BANDWIDTH;
  parameters.tmpdir    = DEFAULT_TMP;
  parameters.array     = "x2";
  
  
//  -B <blksize_kb>   : Set blocksize to blksize_kb kilobytes
/*
  -c <method|l>     : Compression method for reporting (use -c l to list methods)
*/


  Parameters& p = parameters;
  opts.add(showversion,'V',"version"  , ""           , "show version and copyright info");
  opts.add(p.do_help,  'h',"help"     , ""           , "show usage");
  opts.add(dbname,     'd',"db"       , "<file>"     , "database file path (default $HOME/qdda.db)");
  opts.add(g_quiet,    'q',"quiet"    , ""           , "Don't show progress indicator or intermediate results");
  opts.add(p.bandwidth,'b',"bandwidth", "<mb/s>"     , "Throttle bandwidth in MB/s (default 200, 0=disable)");
  opts.add(p.do_mandump,0 ,"mandump"  , ""           , "dump manpage to stdout");
  opts.add(manpage,     0 ,"man"      , ""           , "show manpage");
  opts.add(p.workers,   0 ,"workers"  , "<wthreads>" , "number of worker threads");
  opts.add(p.readers,   0 ,"readers"  , "<rthreads>" , "(max) number of reader threads");
  opts.add(p.buffers,   0 ,"buffers"  , "<buffers>"  , "number of buffers (debug only!)");
  opts.add(p.dryrun,   'n',"dryrun"   , "aaa"           , "skip staging db updates during scan");
  opts.add(p.array,     0 ,"array"    , "<arrayid>"  , "set array type/id");
  opts.add(p.do_purge,  0 ,"purge"    , ""           , "Reclaim unused space in database (sqlite vacuum)");
  opts.add(p.import,    0 ,"import"   , "<file>"     , "import another database (must have compatible metadata)");
  opts.add(p.do_cputest,0 ,"cputest"  , ""           , "Single thread CPU performance test");
  opts.add(p.do_dbtest, 0 ,"dbtest"   , "<testdata>" , "Database performance test");
  opts.add(p.skip,      0 ,"nomerge"  , ""           , "Skip staging data merge and reporting, keep staging database");
  opts.add(p.debug,     0 ,"debug"    , ""           , "Enable debug output");
  opts.add(g_query,     0 ,"queries"  , ""           , "Show SQLite queries and results"); // --show?
  opts.add(p.tmpdir,    0 ,"tempdir"  , "<dir>"      , "Set SQLite TEMPDIR (default /var/tmp");
  opts.add(p.do_delete, 0 ,"delete"   , ""           , "Delete database");
  opts.add(p.append,    0 ,"append"   , ""           , "Append data instead of deleting database");
  opts.add(p.detail,   'x',"detail"   , ""           , "Detailed report (file info and dedupe/compression histograms)");

  rc=opts.parse(argc,argv);
  if(rc) die ("Invalid option");

  if(p.do_help)         { showh(opts); exit(0); }
  else if(p.do_mandump) { mandump(opts); exit(0); }
  
  showtitle();
  if(p.do_delete)  { Database::deletedb(dbname); exit(0); }
  
  // Build filelist
  if(optind<argc || !isatty(fileno(stdin)) ) {
    if (!isatty(fileno(stdin)))
      filelist.open("/dev/stdin");
    for (int i = optind; i < argc; ++i)
      filelist.open(argv[i]);
    if(!parameters.append) { // not appending -> delete old database
      Database::deletedb(dbname);
      QddaDB::createdb(dbname);
    }
  }
  if((action == MenuOpt::dbtest || action == MenuOpt::ptest) && !parameters.append) {
    Database::deletedb(dbname);
    QddaDB::createdb(dbname);
  }    
  QddaDB db(dbname);
  db.setmetadata(parameters.array);
  if(filelist.size()>0) 
    analyze(filelist, db, parameters);
  if(p.do_purge)             { db.vacuum(); exit(0); }
  else if(!p.import.empty()) { import(db,p.import); exit(0); } // import(db,p)
  else if(p.do_cputest)      { cout << "cputest\n"; exit(0); }
  else if(p.do_dbtest)       { cout << "dbtest\n"; exit(0); } //mergetest(db,args,p);
  if(!parameters.skip)       { merge(db,parameters); } // merge staging data if any
  if(parameters.detail)      { reportHistograms(db); }
  else if (!p.skip)          { report(db); }
}
