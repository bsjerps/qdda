/*******************************************************************************
 * Title       : threads.cpp
 * Description : threads module for qdda
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+
 * Disclaimer  : See https://www.gnu.org/licenses/gpl-3.0.txt
 * More info   : http://outrun.nl/wiki/qdda
 ******************************************************************************/

#include <iostream>
#include <fstream>
#include <iomanip>
#include <thread>
#include <string>
#include <mutex>

#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <signal.h>

#include "tools.h"
#include "database.h"
#include "qdda.h"
#include "threads.h"

using std::cout;
using std::cerr;
using std::string;
using std::endl;
using std::flush;

extern bool g_debug;
extern bool g_quiet;
extern bool g_abort;

const int kextra_buffers = 32;
const size_t kbufsize    = 1024;

std::mutex mx_print;

typedef std::lock_guard<std::mutex> Lockguard;

/*******************************************************************************
 * Helper functions
 ******************************************************************************/

// get the process thread id from within a thread
long threadPid() { return (long int)syscall(SYS_gettid); }

// print the thread id (debugging)
void printthread(string& msg) {
  Lockguard lock(mx_print);
  if(g_debug) std::cerr << msg << std::endl << std::flush;
}

/*******************************************************************************
 * Databuffer class - holds <blockspercycle> blocks of data from a stream
 * read by the reader to be processed by the worker
 ******************************************************************************/

DataBuffer::DataBuffer(int64 blocksize, int64 sz_blocks) {
  blocks      = sz_blocks;
  blockbytes  = blocksize * 1024;
  bufsize     = blockbytes * blocks;
  readbuf     = new char[bufsize]();
  used        = 0;
  blockcount  = 0;
  bytes       = 0;
  v_hash.resize(blocks);
  v_bytes.resize(blocks);
}

DataBuffer::~DataBuffer() { delete[] readbuf; }
void DataBuffer::reset()  { used = 0; }

// access to the nth block in the buffer - TBD: range checking!
char* DataBuffer::operator[](int n) {
  return readbuf + (n*blockbytes);
}

/*******************************************************************************
 * IOThrottle class - holds shared data to provide IO bandwidth throttling
 ******************************************************************************/

IOThrottle::IOThrottle(int64 m) { mibps = m; }

// Request to read x kb, microsleep to match required bandwidth
void IOThrottle::request(int64 kb) {
  if(!mibps) return; // immediately return if we don't throttle
  Lockguard lock(mx_throttle);
  stopwatch.lap();
  int64 microsec = (1024 * kb) / mibps;
  if(microsec > stopwatch)
    usleep(microsec - stopwatch);
  stopwatch.reset();
}

/*******************************************************************************
 * Ringbuffer class - Keeps track of multiple buffers between threads
 ******************************************************************************/

RingBuffer::RingBuffer(size_t sz) {
  head = 0;
  tail = 0;
  work = 0;
  size = sz;
  done = false;
  mx_buffer.resize(sz);
}

void RingBuffer::release(size_t ix) {
  mx_buffer[ix].unlock();
}

void RingBuffer::print() {
  Lockguard lock(mx_print);
  cout <<  "T" << std::setw(4) << tail 
       << " W" << std::setw(4) << work 
       << " H" << std::setw(4) << head
       << endl << flush;
}

bool RingBuffer::isFull() {
  Lockguard lock(mx_meta);
  return ((head+1) % size) == tail;
}

bool RingBuffer::hasData() {
  Lockguard lock(mx_meta);
  return work != head;
}

bool RingBuffer::isEmpty() {
  Lockguard lock(mx_meta);
  return tail == work;
}

bool RingBuffer::isDone() {
  Lockguard lock(mx_meta);
  if(done) {
    if(head == tail) return true;
  }
  return false;
}

int RingBuffer::getfree(size_t& ix) {
  if(g_abort) return 2;
  Lockguard lock(headbusy);
  ix = head;
  while (isFull()) {
    usleep(10000);
    if(isDone()) return 1;
    if(g_abort) return 2;
  }
  mx_buffer[ix].lock();
  head = ++head % size; // move head to the next  
  return 0;
}

int RingBuffer::getfull(size_t& ix) {
  if(g_abort) return 2;
  Lockguard lock(workbusy);
  ix = work;
  while(!hasData()) {
    usleep(10000);
    if(isDone()) return 1;
    if(g_abort) return 2;
  }
  mx_buffer[ix].lock();
  work = ++work % size;  // move ptr to next
  return 0;
}

int RingBuffer::getused(size_t& ix) {
  if(g_abort) return 2;
  Lockguard lock(tailbusy);
  ix = tail;
  while (isEmpty()) {
    usleep(10000);
    if(isDone()) return 1;
    if(g_abort) return 2;
  }
  mx_buffer[ix].lock();
  tail = ++tail % size;  // we need the next
  return 0;
}

/*******************************************************************************
 * SharedData class - for easy sharing between threads using only one parameter
 ******************************************************************************/

SharedData::SharedData(int buffers, int files, int64 blksz, StagingDB* db, int bw): 
  throttle(bw), rb(buffers)
{
  blocksize      = blksz;
  blockspercycle = kbufsize / blocksize; // read 1MiB per IO
  blocks         = 0;
  bytes          = 0;
  cbytes         = 0;
  p_sdb          = db;
  v_databuffer.reserve(buffers);
  for(int i=0;i<buffers;i++) {
    DataBuffer* d = new DataBuffer(blocksize, blockspercycle);
    v_databuffer.push_back(*d);
  }
  filelocks = new Mutex[files];
}

SharedData::~SharedData() {
  delete[] filelocks;
}

/*******************************************************************************
 * Updater - reads results from buffers and updates staging database
 ******************************************************************************/

void updater(int thread, SharedData& sd, Parameters& parameters) {
  armTrap();
  pthread_setname_np(pthread_self(),"qdda-updater");
  size_t i=0;
  sd.p_sdb->begin();
  while(true) {
    if(g_abort) break;
    int rc = sd.rb.getused(i);
    if(rc) break;
    //if(g_abort) break;
    if(!parameters.dryrun)
      for(int j=0; j<sd.v_databuffer[i].used; j++)
        sd.p_sdb->insertdata(sd.v_databuffer[i].v_hash[j],sd.v_databuffer[i].v_bytes[j]);
        sd.v_databuffer[i].reset();
    sd.rb.release(i);
  }
  sd.p_sdb->end();
}

/*******************************************************************************
 * Readstream - reads from stream (block/file/pipe) and fills buffers
 ******************************************************************************/

size_t readstream(int thread, SharedData& shared, FileData& fd) {
  int rc;
  int64 blocks;
  size_t bytes;
  size_t totbytes=0;
  const uint64 blocksize = shared.blocksize;
  size_t i;

  size_t iosize = shared.blockspercycle * blocksize * 1024;
  char* readbuf = new char[iosize];
  char* zerobuf = new char[blocksize*1024];
  memset(zerobuf, 0, blocksize*1024);
  srand(thread);
  
  while(!fd.ifs->eof()) {
    if(g_abort) break;
    shared.throttle.request(shared.blockspercycle * blocksize); // IO throttling, sleep if we are going too fast
    
    fd.ifs->read(readbuf, iosize);
    if(fd.ratio)
      for(int i=0; i<iosize/(blocksize*1024);i++) 
        memcpy(readbuf + (i* blocksize * 1024), zerobuf, abs(rand())%(blocksize*1024));

    bytes     = fd.ifs->gcount();
    blocks    = bytes / (blocksize*1024);         // amount of full blocks
    blocks   += bytes % (blocksize*1024) ? 1 : 0; // partial block read, add 1
    totbytes += bytes;

    if(bytes<iosize)  // if we reached eof, clear rest of the buffer
      memset(readbuf + bytes, 0, iosize - bytes);

    for(int j=0;j<(fd.repeat?fd.repeat:1);j++) { // repeat processing the same buffer to simulate duplicates, usually repeat == 1
      rc = shared.rb.getfree(i);
      if(rc) break;
      memcpy(shared.v_databuffer[i].readbuf,readbuf,iosize);
      shared.v_databuffer[i].used = blocks;
      shared.rb.release(i);
    }
    if(fd.limit_mb && totbytes >= fd.limit_mb*1048576) break; // end if we only read a partial file
  }
  fd.ifs->close();
  delete[] zerobuf;
  delete[] readbuf;
  return totbytes;
}

/*******************************************************************************
 * Reader thread - finds one available file and starts readstream
 ******************************************************************************/

void reader(int thread, SharedData& sd, v_FileData& filelist) {
  armTrap();
  string self = "qdda-reader-" + toString(thread,0);
  pthread_setname_np(pthread_self(), self.c_str());
  for(int i=0; i<filelist.size(); i++) {
    if(sd.filelocks[i].trylock()) continue; // in use
    if(filelist[i].ifs->is_open()) {
      size_t bytes = readstream(thread, sd, filelist[i]);
      Lockguard lock(sd.mx_database);
      sd.p_sdb->insertmeta(filelist[i].filename, bytes/sd.blocksize/1024, bytes);
    }
    sd.filelocks[i].unlock();
  }
}

/*******************************************************************************
 * Worker thread - picks filled buffers and runs hash/compression algorithms
 ******************************************************************************/

void worker(int thread, SharedData& sd, Parameters& parameters) {
  armTrap();
  string self = "qdda-worker-" + toString(thread,0);
  pthread_setname_np(pthread_self(), self.c_str());
  const int64 blocksize = sd.blocksize;
  char* dummy           = new char[blocksize*1024];
  size_t i              = 0;
  uint64_t hash;
  int bytes;

  // define function pointer to the right compress function
  u_int (*compress)(const char*,char*,const int);
  switch(sd.method) {
    case Metadata::lz4     : compress = compress_lz4;     break;
    case Metadata::deflate : compress = compress_deflate; break;
    default: compress = compress_none;
  }

  while (true) {
    if(g_abort) break;
    int rc = sd.rb.getfull(i);
    if(rc) break;
    for(int j=0; j < sd.v_databuffer[i].used; j++) {
      if(g_abort) return;
      DataBuffer& r_blockdata = sd.v_databuffer[i]; // shorthand to buffer for readabily
      hash = hash_md5(r_blockdata[j], dummy, blocksize*1024); // get hash
      
      if(rand()%sd.interval==0)
        bytes = hash ? compress(r_blockdata[j], dummy, blocksize*1024) : 0; // get bytes, 0 if hash=0
      else bytes=-1; // special case: -1 means this block was not analyzed for compression

      r_blockdata.v_hash[j] = hash;
      r_blockdata.v_bytes[j] = bytes;
      sd.v_databuffer[i].blockcount++;
      sd.v_databuffer[i].bytes += blocksize*1024;
      {
        Lockguard lock(sd.mx_shared);
        sd.blocks++;
        sd.bytes += blocksize*1024;
      }
      Lockguard lock(mx_print);
      if(sd.blocks%10000==0 || sd.blocks == 10) {
        progress(sd.blocks, blocksize, sd.bytes);           // progress indicator
      }
    }
    sd.rb.release(i);
  }
  delete[] dummy;
}

/*******************************************************************************
 * Analyze function - Setup staging DB, worker and reader threads
 * to start data analyzer
 ******************************************************************************/

void analyze(v_FileData& filelist, QddaDB& db, Parameters& parameters) {
  if(g_debug) cout << "Main thread pid " << getpid() << endl;

  Database::deletedb(parameters.stagingname);
  StagingDB::createdb(parameters.stagingname, db.getblocksize());
  StagingDB stagingdb(parameters.stagingname);

  int workers     = parameters.workers;
  int readers     = std::min( (int)filelist.size(), parameters.readers);
  int buffers     = parameters.buffers ? parameters.buffers : workers + readers + kextra_buffers;

  SharedData sd(buffers, filelist.size(), db.getblocksize(), &stagingdb, parameters.bandwidth);
  sd.interval = db.getinterval();
  sd.method   = db.getmethod();

  if(!g_quiet) cout
    << "Scanning " << filelist.size() << " files, " 
    << readers << " readers, " 
    << workers << " workers, "
    << buffers << " buffers, "
    << parameters.bandwidth << " MB/s max" << endl;

  std::thread updater_thread;
  std::thread reader_thread[readers];
  std::thread worker_thread[workers];
  Stopwatch stopwatch;

  updater_thread = std::thread(updater,0, std::ref(sd), std::ref(parameters));
  for(int i=0; i<workers; i++) worker_thread[i] = std::thread(worker, i, std::ref(sd), std::ref(parameters));
  for(int i=0; i<readers; i++) reader_thread[i] = std::thread(reader, i, std::ref(sd), std::ref(filelist));

  signal(SIGINT, SIG_IGN); // ignore ctrl-c

  for(int i=0; i<readers; i++) reader_thread[i].join(); 
  sd.rb.done = true; // signal workers that reading is complete
  for(int i=0; i<workers; i++) worker_thread[i].join();
  updater_thread.join();

  stopwatch.lap();
  std::stringstream ss;
  ss << " Scanned in " << stopwatch.seconds() << " seconds";
  progress(sd.blocks, sd.blocksize, sd.bytes, ss.str().c_str());
  if(!g_quiet) cout << endl;
  int64 sumblocks = 0;
  int64 sumbytes = 0;
  for(int i=0;i<sd.rb.getsize();i++) {
    sumblocks += sd.v_databuffer[i].blockcount;
    sumbytes  += sd.v_databuffer[i].bytes;
  }
  if(g_debug) cerr << "Blocks processed " << sumblocks 
            << ", bytes = " << sumbytes
            << " (" << std::fixed << std::setprecision(2) << sumbytes/1024.0/1024 << " MiB)" << endl;
  if(g_abort) {
    stagingdb.close();
    Database::deletedb(parameters.stagingname); // delete invalid database if we were interrupted
  }
  resetTrap();
}
