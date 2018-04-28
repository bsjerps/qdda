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

extern bool g_debug;
extern bool g_quiet;
extern bool g_abort;

const int kextra_buffers = 32;
const size_t kbufsize    = 1024;

DataBuffer::DataBuffer(ulong blocksize, ulong blocksps) {
  size = blocksps;
  blocksize_bytes = blocksize * 1024;
  bufsize      = blocksize_bytes * blocksps;
  readbuf      = new char[bufsize]();
  used         = 0;
  blocks       = 0;
  bytes        = 0;
  v_hash.resize(size);
  v_bytes.resize(size);
}

DataBuffer::~DataBuffer() { delete[] readbuf; }
void DataBuffer::reset()  { used = 0; }

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

void RingBuffer::release(size_t ix) {
  mx_buffer[ix].unlock();
}

void RingBuffer::print() {
  #ifndef __DEBUG
  return;
  #else
  mx_print.lock();
  cout <<  "T" << setw(4) << tail 
       << " W" << setw(4) << work 
       << " H" << setw(4) << head
       << endl << flush;
  mx_print.unlock();
  #endif
}
bool RingBuffer::isFull() {
  std::lock_guard<std::mutex> lock(mx_meta);
  return ((head+1) % size) == tail;
}

bool RingBuffer::hasData() {
  std::lock_guard<std::mutex> lock(mx_meta);
  return work != head;
}

bool RingBuffer::isEmpty() {
  std::lock_guard<std::mutex> lock(mx_meta);
  return tail == work;
}

bool RingBuffer::isDone() {
  std::lock_guard<std::mutex> lock(mx_meta);
  if(done) {
    if(head == tail) return true;
  }
  return false;
}


int RingBuffer::getfree(size_t& ix) {
  int rc=0;
  if(g_abort) return 2;
  std::lock_guard<std::mutex> lock(headbusy);
  ix = head;
  while (isFull()) {
    usleep(10000);
    if(isDone()) {rc = 1; break; }
    if(g_abort) {rc = 2; break; }
  }
  if(!rc) {
    mx_buffer[ix].lock();
    head = ++head % size; // move head to the next  
  }
  print();
  return rc;
}

int RingBuffer::getfull(size_t& ix) {
  int rc=0;
  if(g_abort) return 2;
  std::lock_guard<std::mutex> lock(workbusy);
  ix = work;
  while(!hasData()) {
    usleep(10000);
    if(isDone()) {rc = 1; break; }
    if(g_abort) {rc = 2; break; }
  }
  if(!rc) {
    mx_buffer[ix].lock();
    work = ++work % size;  // move ptr to next
  }
  print();
  return rc;
}

int RingBuffer::getused(size_t& ix) {
  int rc=0;
  if(g_abort) return 2;
  std::lock_guard<std::mutex> lock(tailbusy);
  ix = tail;
  while (isEmpty()) {
    usleep(10000);
      if(isDone()) {rc = 1; break; }
    if(g_abort) {rc = 2; break; }
  }
  if(!rc) {
    mx_buffer[ix].lock();
    tail = ++tail % size;  // we need the next
  }
  print();
  return rc;
}

// updater thread, picks processed buffers and saves results
// in staging database
void updater(int thread, SharedData& sd, Parameters& parameters) {
  pthread_setname_np(pthread_self(),"qdda-updater");
  size_t i=0;
  sd.p_sdb->begin();
  while(true) {
    if(g_abort) break;
    int rc = sd.rb.getused(i);
    if(rc) break;
    if(g_abort) return;
    if(!parameters.dryrun)
      for(int j=0; j<sd.v_databuffer[i].used; j++)
        sd.p_sdb->insertdata(sd.v_databuffer[i].v_hash[j],sd.v_databuffer[i].v_bytes[j]);
        sd.v_databuffer[i].reset();
    sd.rb.release(i);
  }
  sd.p_sdb->end();
}

// read a stream(file) into available buffers
ulong readstream(int thread, SharedData& shared, FileData& fd) {
  int rc;
  ulong bytes,blocks;
  ulong totbytes=0;
  const ulong blocksize = shared.blocksize;
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
    blocks    = bytes / blocksize / 1024;         // amount of full blocks
    blocks   += (bytes % blocksize*1024 ? 1 : 0); // partial block read, add 1
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
  delete[] readbuf;
  delete[] zerobuf;
  return totbytes;
}

// reader thread, pick a file if available
// each reader handles 1 file at a time
void reader(int thread, SharedData& sd, v_FileData& filelist) {
  string self = "qdda-reader-" + toString(thread,0);
  pthread_setname_np(pthread_self(), self.c_str());
  for(int i=0; i<filelist.size(); i++) {
    if(sd.filelocks[i].trylock()) continue; // in use
    if(filelist[i].ifs->is_open()) {
      ulong bytes = readstream(thread, sd, filelist[i]);
      sd.p_sdb->insertmeta(filelist[i].filename, bytes/sd.blocksize/1024, bytes);
    }
    sd.filelocks[i].unlock();
  }
}

// worker thread, picks filled buffers and runs hash/compression
// then updates the database and stats counters
void worker(int thread, SharedData& sd, Parameters& parameters) {
  string self = "qdda-worker-" + toString(thread,0);
  pthread_setname_np(pthread_self(), self.c_str());
  const ulong blocksize = sd.blocksize;
  char* dummy           = new char[blocksize*1024];
  size_t i              = 0;
  uint64_t hash;
  ulong bytes;
  while (true) {
    if(g_abort) break;
    int rc = sd.rb.getfull(i);
    if(rc) break;
    for(int j=0; j < sd.v_databuffer[i].used; j++) {
      if(g_abort) return;
      DataBuffer& r_blockdata = sd.v_databuffer[i]; // shorthand to buffer for readabily
      hash = hash_md5(r_blockdata[j], dummy, blocksize*1024); // get hash
      bytes = hash ? compress(r_blockdata[j], dummy, blocksize*1024) : 0; // get bytes, 0 if hash=0
      r_blockdata.v_hash[j] = hash;
      r_blockdata.v_bytes[j] = bytes;
      sd.v_databuffer[i].blocks++;
      sd.v_databuffer[i].bytes += blocksize*1024;
      sd.lock();
      sd.blocks++;
      sd.bytes += blocksize*1024;
      sd.unlock();
      sd.mx_output.lock();
      if(sd.blocks%10000==0 || sd.blocks == 10) {
        progress(sd.blocks, blocksize, sd.bytes);           // progress indicator
      }
      sd.mx_output.unlock();
    }
    sd.rb.release(i);
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
  int buffers     = parameters.buffers ? parameters.buffers : workers + readers + kextra_buffers;
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
  ss << " Scanned in " << stopwatch.seconds() << " seconds";
  progress(sd.blocks, sd.blocksize, sd.bytes, ss.str().c_str());
  if(!g_quiet) cout << endl;
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

