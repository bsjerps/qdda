/*******************************************************************************
 * Title       : threads.cpp
 * Description : threads module for qdda
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+
 * Disclaimer  : See https://www.gnu.org/licenses/gpl-3.0.txt
 * More info   : http://outrun.nl/wiki/qdda
 * -----------------------------------------------------------------------------
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

#include "tools.h"
#include "database.h"
#include "qdda.h"
#include "threads.h"

using namespace std;
// dirty hack to improve readability
// #define string std::string


const ulong iosizex = 1024; // kilobytes per IO

extern bool g_debug;
extern bool g_quiet;
extern bool g_abort;

DataBuffer::DataBuffer(ulong blocksize, ulong blocksps) {
  blocksize_bytes = blocksize * 1024;
  bufsize      = blocksize_bytes * blocksps;
  readbuf      = new char[bufsize];
  used         = 0;
  blocks       = 0;
  bytes        = 0;
  v_hash.resize(blocksps);
  v_bytes.resize(blocksps);
}

DataBuffer::~DataBuffer() { delete[] readbuf; }
void DataBuffer::reset()  { memset(readbuf,0,bufsize); used=0;}

// access to the nth block in the buffer
char* DataBuffer::operator[](int n) {
  return readbuf + (n*blocksize_bytes);
}

IOThrottle::IOThrottle(ulong m) { mibps = m; }

// Request to read x kb, microsleep to match required bandwidth
void IOThrottle::request(ulong kb) {
  if(!mibps) return; // immediately return if we don't throttle
  mutex.lock();
  stopwatch.lap();
  ulong microsec = (1024 * kb) / mibps;
  if(microsec > stopwatch)
    usleep(microsec - stopwatch);
  stopwatch.reset();
  mutex.unlock();
}

SharedData::SharedData(int buffers, int files, ulong blksz, StagingDB* db, int bw): 
  throttle(bw),
  rb(buffers)
{
//  bufsize        = buffers;
  blocksize      = blksz;
  blockspercycle = (iosizex * 1024) / blocksize / 1024;
  blocks         = 0;
  bytes          = 0;
  cbytes         = 0;
  rsleeps        = 0;
  wsleeps        = 0;
  p_sdb          = db;
  v_databuffer.reserve(buffers);
  for(int i=0;i<buffers;i++) {
    DataBuffer* d = new DataBuffer(blocksize, blockspercycle);
    v_databuffer.push_back(*d);
  }
  a_mutex_files = new Mutex[files];
}

SharedData::~SharedData() {
  delete[] a_mutex_files;
}

//int SharedData::size() { return bufsize; }

// get the process thread id from within a thread
long threadPid() { return (long int)syscall(SYS_gettid); }

// print the thread id (debugging)
void printthread(std::mutex& mutex, string msg) {
  mutex.lock();
  if(g_debug) std::cerr << msg << std::endl << std::flush;
  mutex.unlock();
}

RingBuffer::RingBuffer(size_t sz) {
  head = 0;
  tail = 0;
  work = 0;
  size = sz;
  done = false;
  mx_buffer.resize(sz);
}

void RingBuffer::clear(size_t ix) {
  mx_buffer[ix].unlock();
}

bool RingBuffer::cmpheadtail() {
  bool ret;
  mx_meta.lock();
  ret = head == tail;
  mx_meta.unlock();
  return ret;
}

bool RingBuffer::cmpworkhead() {
  bool ret;
  mx_meta.lock();
  ret = work == head;
  mx_meta.unlock();
  return ret;
}

bool RingBuffer::cmptailwork() {
  bool ret;
  mx_meta.lock();
  ret = tail == work;
  mx_meta.unlock();
  return ret;
}

int RingBuffer::getfree(size_t& ix) {
  int rc=0;
  if(g_abort) return 2;
  headbusy.lock();
  ix = head;
  mx_buffer[ix].lock();
  head = ++head % size; // move head to the next  
  while (cmpheadtail()) {
    usleep(10000);
    if(g_abort) { rc = 2; break; }
  }
  headbusy.unlock();
  return 0;
}

int RingBuffer::getfull(size_t& ix) {
  int rc=0;
  if(g_abort) return 2;
  workbusy.lock();
  ix = work;
  if(done && cmpworkhead()) {
    workbusy.unlock();
    return 1;
  }
  mx_buffer[ix].lock();
  work = ++work % size;  // move ptr to next
  while(cmpworkhead()) {
    usleep(10000);
    if(done) break;
    if(g_abort) { rc = 2; break; }
  }
  workbusy.unlock();
  return rc;
}

int RingBuffer::getused(size_t& ix) {
  int rc=0;
  if(g_abort) return 2;
  tailbusy.lock();
  ix = tail;
  if(done && cmptailwork()) {
    tailbusy.unlock();
    return 1;
  }
  mx_buffer[ix].lock();  
  tail = ++tail % size;  // we need the next
  while (cmptailwork()) {
    usleep(10000);
    if(done) break;
    if(g_abort) { rc = 2; break; };
  }
  tailbusy.unlock();
  return rc;
}

// updater thread, picks processed buffers and saves results
// in staging database
void updater(int thread, SharedData& sd, Parameters& parameters) {
  pthread_setname_np(pthread_self(),"qdda-updater");
  size_t idy=0;
  int rc;
  while(true) {
    if(g_abort) break;
    rc = sd.rb.getused(idy);
    if(rc) break;
    if(g_abort) return;
    sd.p_sdb->begin();    
    for(int j=0; j<sd.v_databuffer[idy].used; j++)
      sd.p_sdb->insertdata(sd.v_databuffer[idy].v_hash[j],sd.v_databuffer[idy].v_bytes[j]);
    sd.p_sdb->end();
    sd.v_databuffer[idy].reset();
    sd.rb.clear(idy);
  }
}

// read a stream(file) into available buffers
ulong readstream(int thread, SharedData& shared, FileData& fd) {
  ulong rbytes,blocks;
  ulong totbytes=0;
  const ulong blocksize = shared.blocksize;
  size_t i;
  
  while(!fd.ifs->eof()) {
    if(g_abort) break;
    shared.rb.getfree(i);

    shared.throttle.request(shared.blockspercycle * blocksize); // IO throttling, sleep if we are going too fast
    shared.v_databuffer[i].offset = totbytes; //fd.ifs->tellg()/blocksize/1024;

    fd.ifs->read(shared.v_databuffer[i].readbuf, blocksize*1024*shared.blockspercycle);
    rbytes     = fd.ifs->gcount();
    blocks     = rbytes / blocksize / 1024; // amount of full blocks
    blocks    += rbytes%blocksize*1024?1:0; // partial block read, add 1
    totbytes  += rbytes;
    shared.v_databuffer[i].used = blocks;

    shared.rb.clear(i);
    
    if(fd.limit_mb && totbytes >= fd.limit_mb*1048576) break; // end if we only read a partial file
  }
  fd.ifs->close();
  return totbytes;
}

// reader thread, pick a file if available
// each reader handles 1 file at a time
void reader(int thread, SharedData& sd, v_FileData& filelist) {
  string self = "qdda-reader-" + toString(thread,0);
  pthread_setname_np(pthread_self(), self.c_str());
  for(int i=0; i<filelist.size(); i++) {
    if(sd.a_mutex_files[i].trylock()) continue; // in use
    if(filelist[i].ifs->is_open()) {
      ulong bytes = readstream(thread, sd, filelist[i]);
      sd.p_sdb->insertmeta(filelist[i].filename, bytes/sd.blocksize/1024, bytes);
    }
    sd.a_mutex_files[i].unlock();
  }
}

// worker thread, picks filled buffers and runs hash/compression
// then updates the database and stats counters
void worker(int thread, SharedData& sd, Parameters& parameters) {
  string self = "qdda-worker-" + toString(thread,0);
  pthread_setname_np(pthread_self(), self.c_str());
  const ulong blocksize = sd.blocksize;
  char*  dummy    = new char[blocksize*1024];
  size_t idx=0;
  ulong  hash,bytes;
  int rc;
  while (true) {
    if(g_abort) break;
    rc = sd.rb.getfull(idx);
    if(rc) break;
    // first process all blocks in the buffer
    for(int i=0; i < sd.v_databuffer[idx].used; i++) {
      if(g_abort) return;
      DataBuffer& r_blockdata = sd.v_databuffer[idx]; // shorthand to buffer for readabily
      hash = hash_md5(r_blockdata[i], dummy, blocksize*1024); // get hash
      bytes = hash ? compress(r_blockdata[i], dummy, blocksize*1024) : 0; // get bytes, 0 if hash=0
      r_blockdata.v_hash[i] = hash;
      r_blockdata.v_bytes[i] = bytes;
      sd.v_databuffer[idx].blocks++;
      sd.v_databuffer[idx].bytes += blocksize*1024;
      sd.lock();
      sd.blocks++;
      sd.bytes += blocksize*1024;
      sd.unlock();
      sd.mutex_output.lock();
      if(sd.blocks%10000==0 || sd.blocks == 10) {
        progress(sd.blocks, blocksize, sd.bytes);           // progress indicator
      }
      sd.mutex_output.unlock();
    }
    sd.rb.clear(idx);
  }
  delete[] dummy;
}

/*
 * tbd: look for offsets with a specific hash - replace dump function
 */

// Setup staging DB, worker and reader threads to start data analyzer
void analyze(v_FileData& filelist, QddaDB& db, Parameters& parameters) {
  armTrap(); // handle ctrl-c
  if(g_debug) cout << "Main thread pid " << getpid() << endl;

  Database::deletedb(parameters.stagingname);
  StagingDB::createdb(parameters.stagingname, db.blocksize);
  StagingDB stagingdb(parameters.stagingname);

  int workers     = parameters.workers;
  int readers     = min( (int)filelist.size(), parameters.readers);
  int buffers     = parameters.buffers ? parameters.buffers : workers + readers + 8;
  ulong blocksize = db.blocksize;

  SharedData sd(buffers, filelist.size(), blocksize, &stagingdb, parameters.bandwidth);

  if(!g_quiet) cout
    << "Scanning " << filelist.size() << " files, " 
    << readers << " readers, " 
    << workers << " workers, "
    << buffers << " buffers, "
    << parameters.bandwidth << " MB/s max" << endl;

  thread updater_thread;
  thread reader_thread[readers];
  thread worker_thread[workers];
  Stopwatch stopwatch;

  updater_thread = thread(updater,0, std::ref(sd), std::ref(parameters));
  for(int i=0; i<workers; i++) worker_thread[i] = thread(worker, i, std::ref(sd), std::ref(parameters));
  for(int i=0; i<readers; i++) reader_thread[i] = thread(reader, i, std::ref(sd), std::ref(filelist));

  for(int i=0; i<readers; i++) reader_thread[i].join(); 
  sd.rb.done = true; // signal workers that reading is complete
  for(int i=0; i<workers; i++) worker_thread[i].join();
  updater_thread.join();

  stopwatch.lap();
  stringstream ss;
  ss << " Scanned in " << stopwatch.seconds() << " seconds" << endl << flush;
  progress(sd.blocks, sd.blocksize, sd.bytes, ss.str().c_str());
  ulong sumblocks = 0;
  ulong sumbytes = 0;
  for(int i=0;i<sd.rb.getsize();i++) {
    sumblocks += sd.v_databuffer[i].blocks;
    sumbytes  += sd.v_databuffer[i].bytes;
  }
  if(g_debug) cerr << "Blocks processed " << sumblocks 
            << ", bytes = " << sumbytes
            << " (" << fixed << setprecision(2) << sumbytes/1024.0/1024 << " MiB)" << endl;
  if(g_abort) {
    stagingdb.close();
    Database::deletedb(parameters.stagingname); // delete invalid database if we were interrupted
  }
  resetTrap();
}

