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
 * 1.9.1 - Minor changes
 * ---------------------------------------------------------------------------
 * Build notes: Requires lz4 >= 1.7.1
 ******************************************************************************/

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

#include "tools.h"
#include "qdda.h"

extern "C" {
#include "md5.h"
}

using namespace std;

/*******************************************************************************
 * global parameters - modify at own discretion
 ******************************************************************************/

const char  *progversion   = "1.9.2";
const char  *deftmppath    = "/var/tmp";             // tmpdir for SQLite temp files
const ulong blockspercycle = 64;                     // read chunk blocks at a time when throttling
const ulong updateinterval = 10000;                  // progress report every N blocks
const ulong commitinterval = 20000;                  // commit every N blocks (rows)
const int   col1w          = 15;                     // 1st column - max 9TB without messing up
const int   col2w          = 10;                     // 2nd column

/*******************************************************************************
 * Constants - don't touch
 ******************************************************************************/

const ulong mebibyte      = 1048576; // Bytes per MiB
const ulong def_blocksize = 16384;   // default blocksize
const ulong max_blocksize = 131072;  // max allowed blocksize

/*******************************************************************************
 * Initialization - globals
 ******************************************************************************/

sqlitedb  dbase;                               // global access to database
ofstream  o_debug;                             // Debug stream, open with -D option
ofstream  o_query;                             // Show query stream, disable with -q option
ofstream  o_verbose("/dev/tty");               // Progress stream, disable with -q option
string    database_fn = "/var/tmp/qdda.db";    // default database location

// option parameters/switches
bool   p_norep      = false;  // disable results report
bool   p_dryrun     = false;  // disable SQL updates
bool   p_append     = false;  // don't delete database, just append
bool   p_dump       = false;  // dump block offsets and hashes
ulong  p_bandwidth  = 200;    // default bandwidth throttle (MB/s)
string p_comp       = "";

ulong block_t::blocksize = 0; // static initialization outside class

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

extern char _binary_help_txt_start;
extern char _binary_help_txt_end;
extern char _binary_longhelp_txt_start;
extern char _binary_longhelp_txt_end;

#define extstring(v) (&_binary_##v##_start,&_binary_##v##_end - &_binary_##v##_start)

string helptxt  extstring(help_txt);
string helptxtl extstring(longhelp_txt);


void showtitle() { cout << "qdda " << progversion << title_info << endl; }
void showversion() { std::cout << version_info << std::endl; exit(0); }
void showusage()   { std::cout << helptxt << std::endl; }
void longhelp()    { std::cout << helptxtl << std::endl; }


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

// returns the least significant 6 bytes of the md5 hash (16 bytes) as unsigned long
ulong hash6_md5(const char * buf, const int size) {
  static unsigned char digest[16];
  static int f = 0;
  static char zerobuf[max_blocksize];                // allocate static buffer (once)
  if(!f) { memset(zerobuf,0,max_blocksize); f=1; }   // force hash=0 for zero block
  if(memcmp (buf,zerobuf,size)==0) return 0;         // return 0 for zero block
  MD5_CTX ctx;  
  MD5_Init(&ctx);
  MD5_Update(&ctx, buf, size);
  MD5_Final(digest, &ctx);
  return                            // ignore chars 0-9
    ((ulong)digest[9]  << 48) +     // enable this for 56-bit hashes vs 48-bit
    ((ulong)digest[10] << 40) +     // convert char* to ulong but keeping
    ((ulong)digest[11] << 32) +     // the right order, only use lower 6 bytes (char 10-15)
    ((ulong)digest[12] << 24) +     // SQLite integer is 8 byte signed so we need to stay within
    ((ulong)digest[13] << 16) +     // 8 bytes and unsigned. 6 bytes is best compromise
    ((ulong)digest[14] << 8 ) +
    ((ulong)digest[15]);
}

// Get compressed bytes for a compressed block - lz4
u_int compress(const char * src,const int size) {
  static char buf[max_blocksize+1024];                          // compressed block may be larger than uncompressed
  int result = LZ4_compress_default(src, buf, size, size+1024); // call LZ4 compression lib, only use bytecount & ignore data
  if(result>size) return size;                                  // don't compress if size is larger than blocksize
  if(result==0) die("Compression error");
  return result;
}

// Bandwidth throttle. Sleeps x microseconds between calls, up to the requested cycletime
void throttle(const ulong cycletime) {
  if(!cycletime) return;                                   // immediately return if we don't throttle
  static stopwatch_t stopwatch;                            // static stopwatch to keep track of time between calls
  stopwatch.lap();                                         // get the laptime
  if(cycletime > stopwatch) usleep(cycletime - stopwatch); // sleep to match the required cycletime
  stopwatch.reset();                                       // reset time for next call
}

/*******************************************************************************
 * Formatting & printing
 ******************************************************************************/

// Dump the hash values
void showdump(ulong block, ulong hash,ulong bytes) {
  cout << dec << setw(9) << setfill('0') << block
       << ","  << hex << setw(18) << showbase << internal << hash 
       << ","  << dec << setw(5)  << bytes << endl;
}

// Show scan speed info
void showprogress(const std::string& str) {
  static unsigned int l = 0;
  l = str.length() > l ? str.length() : l;     // track largest string length between calls
  if(str.length() == 0) {                      // clear line if string empty
    for(u_int i=0;i<l;i++) o_verbose << ' ';   // overwrite old line with spaces
    for(u_int i=0;i<l;i++) o_verbose << '\b';  // carriage return (no newline), \b = backspace
  } else {
    o_verbose << str;
    for(u_int i=0;i<str.length();i++) o_verbose << '\b'; // returns the cursor to original offset
    o_verbose << std:: flush;                            // print string ; carriage return
  }
}

// Show progress information, updated every N blocks. 
void progress(ulong blocks,ulong blocksize, ulong bytes = 0, const char * msg = NULL) {
  static stopwatch_t stopwatch;     // time of start processing file
  static stopwatch_t prev;          // time of previous call
  stopwatch.lap();                  // get current time interval
  static int   filenum   = 0;       // previous file num (tells us when new file process starts)
  static ulong prevbytes = 0;       // keep track of previous byte count
  if(blocks==0) {                   // reset stats before processing new file
    stopwatch.reset();
    prev=stopwatch;
    prevbytes=0;
    filenum++;
  }
  auto avgsvctm = stopwatch;                                // service time of xx bytes since start of file
  auto cursvctm = stopwatch - prev;                         // service time of xx bytes since previous call
  auto avgbw = safediv_ulong(bytes,avgsvctm);               // bytes per second since start of file
  auto curbw = safediv_ulong((bytes-prevbytes),cursvctm);   // bytes per second since previous call
  stringstream ss;                                          // generate a string with the progress message
  ss << "File " << setfill('0') << setw(2) << filenum       // file #
     << ", " << blocks << " "                               // blocks scanned
     << blocksize/1024 << "k blocks ("                      // blocksize
     << bytes/mebibyte << " MiB) processed, "               // processed megabytes
     << curbw << "/"                                        // current bandwidth
     << (p_bandwidth?p_bandwidth:9999) << " MB/s, "         // max bandwidth or 9999 if no throttle
     << avgbw << " MB/s avg";                               // avg bandwidth
  if(msg) ss << msg;                                        // add message if specified
  ss << "                 " ;                               // blank rest of line
  showprogress(ss.str());
  prev=stopwatch;  // save stopwatch time for next call
  prevbytes=bytes; // save byte count for next call
}

/*******************************************************************************
 * Functions
 ******************************************************************************/

// refuse to set blocksize too high
ulong get_blocksize(const char * in) {
  ulong bsize = atol(in)*1024;
  if(bsize>max_blocksize) die("Blocksize exceeded");
  return bsize;
}

// Auto determine compression method based on blocksize if not set by parameter
// currently 8KB = XtremIO V1, 16KB=XtremIO V2, 128KB = VMAK AFA (beta)
void set_method() {
  uppercase(p_comp);
  if(p_comp == "L") {
    dbase.runquery("5,8,8,11","select type as Type, desc as Description from bucketdesc where type != ''"); 
    exit(0);
  }
  if(p_comp.length()) return;
  auto blksz = dbase.blocksize();
  switch(blksz) {
    case 8192:   p_comp="X1"; break;
    case 16384:  p_comp="X2"; break;
    case 131072: p_comp="V1"; break;
  }
}

// Merge staging data into kv table, track & display time to merge
void merge() {
  if(dbase.attach()) return;
  stringstream ss1,ss2;
  auto blksz   = dbase.blocksize();
  auto dbrows  = dbase.rows();
  auto tmprows = dbase.tmprows();
  auto mib_staging = tmprows*blksz/mebibyte;
  auto mib_database = dbrows*blksz/mebibyte;
  stopwatch_t stopwatch;
  if(tmprows) { // do nothing if merge db has no rows
    o_verbose << "Merging " << tmprows << " blocks (" << mib_staging << " MiB) with " << dbrows << " blocks (" << mib_database << " MiB)" << endl;
    ss1 << "Indexing" << flush;
    showprogress(ss1.str());
    stopwatch.reset();
    dbase.sql("create index if not exists tmpdb.staging_ix on staging(k,b)");
    stopwatch.lap();
    ulong index_rps = tmprows*1000000/stopwatch;
    ulong index_mbps = mib_staging*1000000/stopwatch;
    ss1 << " in " << stopwatch.seconds() << " sec (" << index_rps << " blocks/s, " << index_mbps << " MiB/s)";
    ss2 << ss1.str() << ", Joining" << flush;
    showprogress(ss2.str());
    stopwatch.reset();
    dbase.merge();
    stopwatch.lap();
    auto time_merge = stopwatch;
    ulong merge_rps = tmprows*1000000/time_merge;
    ulong merge_mbps = mib_staging*1000000/time_merge;
    ss2 << " in " << stopwatch.seconds() << " sec (" << merge_rps << " blocks/s, " << merge_mbps << " MiB/s)";
    o_verbose << ss2.str() << endl;
  }
  dbase.detach(true); // detach and delete staging
}

// process file
void analyze(const char * filename) {
  dbase.attach();                 // open and initialize temp db for staging
  auto blksz = dbase.blocksize(); // get blocksize from db
  char buf[max_blocksize];        // working buffer
  std::ifstream f;                // file to be processed
  ulong cycletime = 0;            // throttle cycletime in usec
  ulong hash      = 0;            // crc32 will be stored here
  ulong blocks    = 0;            // block counter
  ulong bytes     = 0;            // byte counter
  uint  cbytes    = 0;            // compressed bytes
  // std:: string fname = filename;  // alt. filename
  cycletime = safediv_ulong(blksz*blockspercycle,p_bandwidth); // throttle cycletime
  f.open(filename);
  if (!f.is_open()) die("File open failed: " + (string)filename);
  progress(0,blksz);                             // reset progress counter
  dbase.begin();                                 // begin transaction (performance)
  while (!f.eof()) {                             // start looping through file blocks
    cbytes = 0;                                  // 0 for zero block
    memset(buf,0,blksz);                         // clear buffer
    f.read(buf,blksz);                           // read 1 block
    if(f.gcount()==0) break;                     // zero bytes read = end reached
    bytes += f.gcount();                         // add bytes processed
    hash = hash6_md5(buf,blksz);                 // get the md5 hash (lower 6 bytes), or 0 if zero block
    if(hash)      cbytes = compress(buf,blksz);  // get compressed size
    if(p_dump)    showdump(blocks,hash,cbytes);  // dump hashes with -d option
    if(!p_dryrun) dbase.insert(hash,cbytes);     // insert or update into database temp table
    blocks++;                                    
    if(blocks%commitinterval==0) dbase.commit();               // commit every n rows
    if(blocks%updateinterval==0) progress(blocks,blksz,bytes); // progress indicator
    if(blocks%blockspercycle==0) throttle(cycletime);          // throttle bandwidth
  };
  dbase.end();                                   // end transaction
  progress(blocks,blksz,bytes);
  dbase.savemeta(filename,blocks,bytes);         // store file metadata
  o_verbose << std::endl;
}

// import data from another qdda database
void import(const char * fn) {
  stringstream q_attach;
  q_attach << "attach database '" << fn << "' as tmpdb";
  dbase.sql(q_attach.str());
  auto blksz1  = dbase.select_long("select blksz from metadata");
  auto blksz2  = dbase.select_long("select blksz from tmpdb.metadata");
  auto blocks1 = dbase.select_long("select count(*) from kv");
  auto blocks2 = dbase.select_long("select count(*) from tmpdb.kv");
  if(blksz1 != blksz2) die("Incompatible blocksize");
  cout << "Adding " << blocks2 << " blocks from " << fn << " to " << blocks1 << " existing blocks" << endl;
  dbase.import();
  dbase.sql("insert into files(name,blocks,size) select name,blocks,size from tmpdb.files");
}

// test merging performance with random data
void mergetest(ulong gb) {
  const ulong blocksize = dbase.blocksize();
  const ulong rowspergb = 1024 * mebibyte / blocksize;
  const ulong rows       = rowspergb * gb;
  srand (time(NULL));
  dbase.attach();
  stopwatch_t stopwatch;
  cout << "Merge test: Loading " << rows << " " << blocksize/1024 << "k random blocks (" << rows*blocksize/mebibyte << " MiB) " << flush;
  for(ulong i=0;i<rows;i++) dbase.insert(lrand() & 0x00FFFFFFFFFFFFFF,rand() % blocksize);
  stopwatch.lap();
  cout << "in " << stopwatch.seconds() << " s" << endl;
  dbase.savemeta("mergetest", rows, rows*blocksize);
  if(!p_norep) merge();
}

// test hashing, compression and insert performance
void speedtest() {
  const ulong gb        = 2; // size of test set
  const ulong blocksize = dbase.blocksize();
  const ulong rows      = gb * 1024 * mebibyte / blocksize;
  const ulong bufsize   = gb * 1024 * mebibyte;
  char       *testdata  = new char[bufsize];
  stopwatch_t stopwatch;

  cout << fixed << setprecision(2) << "*** Synthetic performance test ***" << endl;

  cout << "Initializing:" << flush; 
  srand(1);
  memset(testdata,0,bufsize);
  for(ulong i=0;i<bufsize;i++) testdata[i] = (char)rand() % 256; // fill test buffer with random data
  cout << setw(col1w) << rows << " blocks, 16k (" << bufsize/mebibyte << " MiB)" << endl;

  cout << "Hashing:     " << flush;
  stopwatch.reset();
  for(ulong i=0;i<rows;i++) hash6_md5(testdata + i*blocksize,blocksize);
  auto time_hash = stopwatch.lap();
  
  cout << setw(col1w) << time_hash     << " usec, " << setw(col2w) << (float)bufsize/time_hash     << " MB/s" << endl;

  cout << "Compressing: " << flush;
  stopwatch.reset();
  for(ulong i=0;i<rows;i++) compress(testdata + i*blocksize,blocksize);
  auto time_compress = stopwatch.lap();
  cout << setw(col1w) << time_compress << " usec, " << setw(col2w) << (float)bufsize/time_compress << " MB/s" << endl;
  
  // test sqlite insert performance
  dbase.attach();
  cout << "DB insert:   " << flush;
  stopwatch.reset();
  dbase.begin();
  for(ulong i=0;i<rows;i++) dbase.insert(i,blocksize);
  dbase.end();
  auto time_insert = stopwatch.lap();
  cout << setw(col1w) << time_insert   << " usec, " << setw(col2w) << (float)bufsize/time_insert   << " MB/s" << endl;

  auto time_total = time_hash + time_compress + time_insert;
  cout << "Total:       " << setw(col1w) << time_total    << " usec, " << setw(col2w) << (float)bufsize/time_total    << " MB/s" << endl;

  dbase.detach(true);
  delete[] testdata;
}

// parse arg and call either speedtest (arg=0) or mergetest
void perftest(const char *arg) {
  uint mergesize = atol(arg);
  if(mergesize) mergetest(mergesize);
  else speedtest();
}

/*******************************************************************************
 * Reports
 ******************************************************************************/

// max bucketsize must match blocksize for compression estimate
void checkcompression() {
  if(!p_comp.length()) return;      // ignore if we have no compression method
  auto blocksize=dbase.blocksize();
  auto maxbucket=dbase.select_long("select max(max) from buckets where type='" + p_comp + "'");
  if(blocksize!=maxbucket) die("Incompatible blocksize for compression type " + p_comp);
}

// show value as string - formatted/aligned megabytes string
std::string block_t::mib() {
  stringstream ss;
  ss << setfill(' ') << setw(11) << to_string((float)blocksize*blocks/mebibyte,2) << " MiB";
  return ss.str(); 
}

// show value as string - formatted/aligned blocks
std::string block_t::show() {
  stringstream ss;
  ss << " (" << setw(col2w) << blocks << " blocks)";
  return ss.str(); 
}

// show ratio as right column formatted percentage
std::string ratio_t::showpercf() {
  stringstream ss;
  ss << " (" << setw(col2w) << to_string(p*100,2) << " %)";
  return ss.str();
}

// show ratio as left column formatted percentage
std::string ratio_t::showperc() {
  stringstream ss;
  ss << setw(col2w+1) << to_string(p*100,2) << " %";
  return ss.str();
}

// show ratio as right column formatted ratio
std::string ratio_t::show() {
  stringstream ss;
  ss << setw(col2w+1) << to_string(p,2);
  return ss.str();
}

// Print stats report
void report() {
  os_reset();          // reset std::cout state
  merge();             // merge staging data if any
  set_method();        // Set compression 
  checkcompression();  // Match blocksize with compression method

  auto blocksize     = dbase.blocksize();                                               // Blocksize used when scanning
  string compdesc    = dbase.select_text("select desc from bucketdesc where type='" + p_comp + "'"); // compression description
  block_t::setblocksize(blocksize);
  block_t blk_total  = dbase.select_long("select sum(v) from kv");                      // Total scanned blocks
  block_t blk_zero   = dbase.select_long("select v from kv where k=0");                 // Total zero blocks
  block_t blk_used   = dbase.select_long("select sum(v) from kv where k!=0");           // Total non-zero blocks
  block_t blk_hashes = dbase.select_long("select count(*) from kv where k>0");          // Unique hashes (deduped)
  block_t blk_count1 = dbase.select_long("select count(v) from kv where k>0 and v=1");  // Hashes with count=1 (non-dedupable data)
  block_t blk_countx = dbase.select_long("select count(v) from kv where k>0 and v>1");  // Hashes with count=1 (non-dedupable data)
  block_t blk_merged = dbase.select_long("select sum(v-1) from kv where k>0 and v>1");  // merged (saved) with dedupe
  block_t blk_count2 = dbase.select_long("select count(v) from kv where k>0 and v=2");  // Hashes with count=2 (dedupable data)
  block_t blk_counth = dbase.select_long("select count(v) from kv where k>0 and v>2");  // Hashes with count>2 (dedupable data)
  block_t compr_pre  = dbase.select_long("select sum(b*v)/(select blksz from metadata) from kv where k>0"); // compression before dedup
  block_t compr_post = dbase.select_long("select sum(b)/(select blksz from metadata) from kv where k>0");   // compression after dedup
  block_t blk_alloc  = dbase.compressed(p_comp.c_str());                                // allocated blocks after sorting into buckets

  // calc ratios - divide by zero results in value 0
  ratio_t used_ratio  = safediv_float (blk_used,   blk_total);             // % used vs total
  ratio_t free_ratio  = safediv_float (blk_zero,   blk_total);             // % free vs total
  ratio_t dedup_ratio = safediv_float (blk_used,   blk_hashes);            // dedupe ratio
  ratio_t thin_ratio  = safediv_float (blk_total,  blk_used);              // % free vs used (thin provisioning)
  ratio_t compr_dedup = safediv_float (blk_hashes, blk_alloc);             // deduped compression ratio (sorted into slots)
  ratio_t pre_ratio   = 1 - safediv_float (compr_pre.bytes(), blk_used.bytes());    // compression ratio (no dedupe)
  ratio_t post_ratio  = 1 - safediv_float (compr_post.bytes(), blk_hashes.bytes()); // compression ratio (with dedupe)
  ratio_t total_ratio = dedup_ratio*compr_dedup*thin_ratio;

  // dump the formatted report
  cout    << "                      " << "*** Overview ***"
  << endl << "total               = " << blk_total.mib()    << blk_total.show()
  << endl << "used                = " << blk_used.mib()     << blk_used.show()
  << endl << "deduped             = " << blk_hashes.mib()   << blk_hashes.show()
  << endl << "allocated           = " << blk_alloc.mib()    << blk_alloc.show()
  << endl << "                      " << "*** Details ***"
  << endl << "Compression method  = " << setw(col1w) << compdesc
  << endl << "blocksize           = " << setw(col1w) << to_string(blocksize/1024) + " KiB"
  << endl << "free (zero)         = " << blk_zero.mib()     << blk_zero.show()
  << endl << "compress pre dedup  = " << compr_pre.mib()    << pre_ratio.showpercf()
  << endl << "merged by dedupe    = " << blk_merged.mib()   << blk_merged.show()
  << endl << "compress post dedup = " << compr_post.mib()   << post_ratio.showpercf()
  << endl << "unique data         = " << blk_count1.mib()   << blk_count1.show()
  << endl << "duped 2:1           = " << blk_count2.mib()   << blk_count2.show()
  << endl << "duped >2:1          = " << blk_counth.mib()   << blk_counth.show()
  << endl << "duped total         = " << blk_countx.mib()   << blk_countx.show()
  << endl << "                      " << "*** Summary ***"
  << endl << "percentage used     = " << used_ratio.showperc()
  << endl << "percentage free     = " << free_ratio.showperc()
  << endl << "deduplication ratio = " << dedup_ratio.show()
  << endl << "compression ratio   = " << compr_dedup.show()
  << endl << "thin ratio          = " << thin_ratio.show()
  << endl << "combined            = " << total_ratio.show()
  << endl << "raw capacity        = " << blk_total.mib()
  << endl << "net capacity        = " << blk_alloc.mib()
  << endl;
}

// print extended report with file info and dedupe/compression histograms
void extreport() {
  checkcompression(); // Match blocksize with compression method
  set_method();       // Set default compression method if needed
  cout << endl;

  string query = R"(select max/1024 as 'Bucket(KiB)', count(b) as Buckets,count(b)*max/(select blksz from metadata) as Blocks, count(b)*max as Bytes from buckets 
left outer join kv on kv.b between min and max where buckets.type='TYPE'
group by max,max)";

  searchreplace(query,"TYPE",p_comp);

  dbase.runquery("5,8,8,11","select id as File, blksz/1024 as Blksz, blocks as Blocks, size/1024/1024 as MiB, name as Filename from files,metadata");

  cout << endl << "Dedupe histogram:" << endl;
  dbase.runquery("12,12","select v as 'Dupcount',count(v) as Blocks,count(v)*(select blksz from metadata) as Bytes from kv where k!=0 group by 1 order by v");

  cout << endl << "Compression Histogram (" 
       << dbase.select_text("select desc from bucketdesc where type='" + p_comp + "'")
       << "): " << endl;
  dbase.runquery("12,12,12",query);
}

/*******************************************************************************
 * Main section - process options etc
 ******************************************************************************/

int main(int argc, char** argv) {
  showtitle();
  int c; const char * p = NULL; // variables for parameter processing
  ulong blocksize = def_blocksize;
  MenuOpt::type action = MenuOpt::none;
  while ((c = getopt(argc, (char **)argv, "+B:DPQT:ab:c:df:hHi:pqrt:Vx?")) != -1) {
    switch(c) {
      case 'B': blocksize = get_blocksize(optarg);  break; // Change default blocksize
      case 'D': o_debug.open("/dev/tty");           break; // Open debug stream
      case 'Q': o_query.open("/dev/tty");           break; // Open query stream
      case 'P': action = MenuOpt::vacuum;           break; // Purge unused space in DB
      case 'i': action = MenuOpt::import; p=optarg; break; // Import from another database
      case 't': action = MenuOpt::test;   p=optarg; break; // Performance test
      case 'V': action = MenuOpt::version;          break; // Show version
      case 'x': action = MenuOpt::xrep;             break; // Extended report
      case 'T': dbase.tempdir(optarg);              break; // Set SQLite tempdir
      case 'a': p_append    = true;                 break; // Keep existing DB data, append new
      case 'b': p_bandwidth=atol(optarg);           break; // Limit bandwidth
      case 'c': p_comp      = optarg;               break; // Compress method
      case 'd': p_dump      = true;                 break; // Dump hashes
      case 'f': database_fn = optarg;               break; // Set DB filename
      case 'n': p_dryrun    = true;                 break; // Don't update DB
      case 'q': o_verbose.close();                  break; // Be quiet (no status updates)
      case 'r': p_norep     = true;                 break; // Skip report and merge
      case 'H': longhelp(); exit(5);                break; // Long help
      case 'h':                                            // Help
      case '?': showusage(); exit(0);
      default : showusage(); exit(10) ;
    }
  }
  if(optind<argc || !isatty(fileno(stdin)) ) {                 // process files if we have files or stdin
    if (p_append==false) sqlite_delete(database_fn.c_str());   // delete database file at start unless we append
    dbase.open(database_fn,blocksize);
    if (!isatty(fileno(stdin)))         analyze("/dev/stdin"); // analyze from stdin if not console
    for (int i = optind; i < argc; ++i) analyze(argv[i]);      // analyze the rest of the files
    if(!p_norep) report();
  } else {
    dbase.open(database_fn,blocksize);
    switch(action) {
      case MenuOpt::none:    report();       break;
      case MenuOpt::import:  import(p);      break;
      case MenuOpt::vacuum:  dbase.vacuum(); break;
      case MenuOpt::test:    perftest(p);    break;
      case MenuOpt::xrep:    extreport();    break;
      case MenuOpt::version: showversion();  break;
    }
  }
}
