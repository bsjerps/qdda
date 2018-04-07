/*******************************************************************************
 * Title       : threads.cpp
 * Description : threads module for qdda
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+
 * Disclaimer  : See https://www.gnu.org/licenses/gpl-3.0.txt
 * More info   : http://outrun.nl/wiki/qdda
 * -----------------------------------------------------------------------------
 ******************************************************************************/

#include <semaphore.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <thread>
#include <string.h>

#include "tools.h"
#include "database.h"
#include "qdda.h"
#include "threads.h"

using namespace std;

const ulong iosizex = 1024; // kilobytes per IO

extern bool g_debug;
extern bool g_quiet;
extern bool g_abort;

DataBuffer::DataBuffer(ulong blocksize, ulong blocksps) {
  // ulong blockspercycle = (iosizex * 1024) / blocksize ;
  blocksize_kb = blocksize * 1024;
  bufsize      = blocksize_kb * blocksps;
  readbuf      = new char[bufsize];
  used         = 0;
  blocks       = 0;
  bytes        = 0;
}

DataBuffer::~DataBuffer() { delete[] readbuf; }
void DataBuffer::reset()  { memset(readbuf,0,bufsize); used=0; }
int DataBuffer::trylock() { return mutex.trylock(); }
void DataBuffer::unlock() { mutex.unlock(); }

// access to the nth block in the buffer
char* DataBuffer::operator[](int n) {
  return readbuf + (n*blocksize_kb);
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
  throttle(bw)
{
  bufsize        = buffers;
  blocksize      = blksz;
  blockspercycle = (iosizex * 1024) / blocksize / 1024;
  read_completed = false;
  blocks         = 0;
  bytes          = 0;
  cbytes         = 0;
  rsleeps        = 0;
  wsleeps        = 0;
  p_sdb          = db;
  v_databuffer.reserve(bufsize);
  for(int i=0;i<bufsize;i++) {
    DataBuffer* d = new DataBuffer(blocksize, blockspercycle);
    v_databuffer.push_back(*d);
  }
  a_mutex_files = new Mutex[files];
}

SharedData::~SharedData() {
  delete[] a_mutex_files;
}

int SharedData::size() { return bufsize; }

// get the process thread id from within a thread
long threadPid() { return (long int)syscall(SYS_gettid); }

// print the thread id (debugging)
void printthread(Mutex& mutex, string msg) {
  mutex.lock();
  if(g_debug) std::cerr << msg << std::endl << std::flush;
  mutex.unlock();
}

// Request a free buffer - lock it and return the index
// or spinwait forever until one is available
int getFreeBuffer(SharedData& sd) {
  while(true) {
    for(int i=0; i < sd.size() ; i++) {
      if(sd.v_databuffer[i].trylock()) continue; // in use
      if(sd.v_databuffer[i].used==0) return i;   // lock ack'ed and buffer is empty
      sd.v_databuffer[i].unlock();               // buffer is full, release lock
    }
    usleep(10000);          // no available buffers, sleep
    sd.mutex_self.lock();   // increase the wait counter to measure
    sd.rsleeps++;           // if we are CPU bound
    sd.mutex_self.unlock();
    if(g_abort) return 0;   // stop reading if we are interrupted (ctrl-c)
  }
}

// Request a filled buffer - lock it and return the index
// or spinwait forever until one is available or if the readers are done is completed
int getUsedBuffer(SharedData& sd, int& idx) {
  while(true) {
    bool flag = sd.read_completed; // readers are done, walk through buffers once more
    for(idx=0; idx < sd.size() ; idx++) {
      if(sd.v_databuffer[idx].trylock()) continue; // in use
      if(sd.v_databuffer[idx].used!=0) return 0;   // lock ack'ed and buffer is not empty
      sd.v_databuffer[idx].unlock();               // buffer is empty, release lock
    }
    usleep(10000);           // no available buffers, sleep
    sd.mutex_self.lock();    // increase the wait counter to measure
    sd.wsleeps++;            // if we are I/O bound
    sd.mutex_self.unlock();
    if(flag) return 1; // readers were done and we handled all remaining data
  }
}

// read a stream(file) into available buffers
ulong readstream(int thread, SharedData& shared, FileData& fd) {
  ulong rbytes,blocks;
  ulong totbytes=0;
  const ulong blocksize = shared.blocksize;
  while(!fd.ifs->eof()) {
    int i=getFreeBuffer(shared);
    if(g_abort) return 0;
    shared.throttle.request(shared.blockspercycle * blocksize); // IO throttling, sleep if we are going too fast
    fd.ifs->read(shared.v_databuffer[i].readbuf, blocksize*1024*shared.blockspercycle);
    rbytes     = fd.ifs->gcount();
    blocks     = rbytes / blocksize / 1024; // amount of full blocks
    blocks    += rbytes%blocksize*1024?1:0; // partial block read, add 1
    totbytes  += rbytes;
    shared.v_databuffer[i].used = blocks;
    shared.v_databuffer[i].unlock();
    if(fd.limit_mb && totbytes >= fd.limit_mb*1048576) return totbytes;
  }
  return totbytes;
}

// reader thread, pick a file if available
// each reader handles 1 file at a time
void reader(int thread, SharedData& sd, v_FileData& filelist) {
  string self = "qdda-reader-" + toString(thread,0);
  pthread_setname_np(pthread_self(), self.c_str());
  int rc;
  for(int i=0; i<filelist.size(); i++) {
    rc=sd.a_mutex_files[i].trylock();
    if(rc==0) {
      //ulong bytes = readstream(thread, sd, filelist[i].ifs);
      ulong bytes = readstream(thread, sd, filelist[i]);
      sd.p_sdb->insertmeta(filelist[i].filename, bytes/sd.blocksize/1024, bytes);
    }
    // deliberately refusing to unlock so files never get read 
    // multiple times by another thread
  }
}

// worker thread, picks filled buffers and runs hash/compression
// then updates the database and stats counters
void worker(int thread, SharedData& sd, Parameters& parameters) {
  string self = "qdda-worker-" + toString(thread,0);
  pthread_setname_np(pthread_self(), self.c_str());
  const ulong blocksize = sd.blocksize;
  ulong* p_hash   = new ulong[sd.blockspercycle];
  ulong* p_cbytes = new ulong[sd.blockspercycle];
  char* dummy     = new char[blocksize*1024];
  int idx=0;
  for(;;) {
    if(getUsedBuffer(sd,idx)) break; // get filled buffer or end loop if processing is complete
    // first process all blocks in the buffer
    for(int i=0; i < sd.v_databuffer[idx].used; i++) {
      if(g_abort) return;
      DataBuffer& r_blockdata = sd.v_databuffer[idx]; // shorthand to buffer for readabily
      p_hash[i]   = hash_md5(r_blockdata[i], dummy, blocksize*1024); // get hash
      p_cbytes[i] = p_hash[i] ? compress(r_blockdata[i], dummy, blocksize*1024) : 0; // get bytes, 0 if hash=0
      sd.v_databuffer[idx].blocks++;
      sd.v_databuffer[idx].bytes += blocksize*1024;
      sd.mutex_self.lock();
      sd.blocks++;
      sd.bytes += blocksize*1024;
      sd.mutex_self.unlock();
      sd.mutex_output.lock();
      if(sd.blocks%10000==0 || sd.blocks == 10) {
        progress(sd.blocks, blocksize, sd.bytes);           // progress indicator
//      if(fileSystemFree(sd.p_sdb->filename()) < 256)      // TBD: abort if FS almost full
//        die(string("\nfile system almost full! - ") + sd.p_sdb->filename());
      }
      sd.mutex_output.unlock();
    }
    // batch store multiple results in the database (speed)
    if(!parameters.dryrun) {
      sd.mutex_database.lock();
      sd.p_sdb->begin();
      for(int i=0; i<sd.v_databuffer[idx].used; i++)
        sd.p_sdb->insertdata(p_hash[i],p_cbytes[i]);
      sd.p_sdb->end();
      sd.mutex_database.unlock();
    }
    sd.v_databuffer[idx].reset();
    sd.v_databuffer[idx].unlock();
  }
  delete[] p_hash;
  delete[] p_cbytes;
  delete[] dummy;
}

/*
 * tbd: look for offsets with a specific hash - replace dump function
 */

// Setup staging DB, worker and reader threads to start data analyzer
void analyze(v_FileData& filelist, QddaDB& db, Parameters& parameters) {
  armTrap(); // handle ctrl-c
  if(g_debug) cout << "Main thread pid " << getpid() << endl;
  string tmpname = parameters.tmpdir + "/qdda-staging.db";
  Database::deletedb(tmpname);
  StagingDB::createdb(tmpname, db.blocksize);
  StagingDB stagingdb(tmpname);

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

  thread reader_thread[readers];
  thread worker_thread[workers];
  Stopwatch stopwatch;

  for(int i=0; i<workers; i++) worker_thread[i] = thread(worker, i, std::ref(sd), std::ref(parameters));
  for(int i=0; i<readers; i++) reader_thread[i] = thread(reader, i, std::ref(sd), std::ref(filelist));

  for(int i=0; i<readers; i++) reader_thread[i].join();
  sd.read_completed = true; //signal workers that reading is complete
  for(int i=0; i<workers; i++) worker_thread[i].join();

  stopwatch.lap();
  stringstream ss;
  ss << " Scanned in " << stopwatch.seconds() << " seconds" << endl << flush;
  progress(sd.blocks, sd.blocksize, sd.bytes, ss.str().c_str());
  ulong sumblocks = 0;
  ulong sumbytes = 0;
  for(int i=0;i<sd.size();i++) {
    sumblocks += sd.v_databuffer[i].blocks;
    sumbytes  += sd.v_databuffer[i].bytes;
  }
  if(g_debug) cerr << "Blocks processed " << sumblocks 
            << ", bytes = " << sumbytes
            << " (" << fixed << setprecision(2) << sumbytes/1024.0/1024 << " MiB)" << endl;
  if(g_abort) {
    stagingdb.close();
    Database::deletedb(tmpname); // delete invalid database if we were interrupted
  }
  resetTrap();
}

