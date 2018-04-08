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

const ulong iosizex = 1024; // kilobytes per IO

extern bool g_debug;
extern bool g_quiet;
extern bool g_abort;

DataBuffer::DataBuffer(ulong blocksize, ulong blocksps) {
  blocksize_kb = blocksize * 1024;
  bufsize      = blocksize_kb * blocksps;
  readbuf      = new char[bufsize];
  v_hash.resize(blocksps);
  v_bytes.resize(blocksps);
  used         = 0;
  status       = 0;
  blocks       = 0;
  bytes        = 0;
}

DataBuffer::~DataBuffer() { delete[] readbuf; }
void DataBuffer::reset()  { memset(readbuf,0,bufsize); used=0;}
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
  work_completed = false;
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
void printthread(std::mutex& mutex, string msg) {
  mutex.lock();
  if(g_debug) std::cerr << msg << std::endl << std::flush;
  mutex.unlock();
}

// Request a free buffer - lock it and return the index
// or spinwait forever until one is available
int getFreeBuffer(SharedData& sd, int& idx) {
  static int prev = 0;
  static Mutex mutex;
  int offset;
  mutex.lock();
  offset = prev + 1;
  mutex.unlock();
  while(true) {
    for(int i=0; i < sd.size() ; i++) {
      idx = (i+offset) % sd.size();
      if(sd.v_databuffer[idx].trylock()) continue; // in use
      if(sd.v_databuffer[idx].status==0) {
        mutex.lock();
        prev = idx;
        mutex.unlock();
        return 0;  // lock ack'ed and buffer is empty
      }
      sd.v_databuffer[idx].unlock(); // buffer is full, release lock
    }
    //cout << "w" << flush;
    usleep(10000);        // no available buffers, sleep
    if(g_abort) return 1; // stop reading if we are interrupted (ctrl-c)
  }
}

int getProcessedBuffer(SharedData& sd, int& idx) {
  static int prev = 0;
  static Mutex mutex;
  int offset;
  mutex.lock();
  offset = prev + 1;
  mutex.unlock();
  while(true) {
    bool flag = sd.work_completed; // workers are done, walk through buffers once more
    for(int i=0; i < sd.size() ; i++) {
      idx = (i+offset) % sd.size();
      if(sd.v_databuffer[idx].trylock()) continue; // in use, try next
      if(sd.v_databuffer[idx].status==2) { // lock ack'ed and buffer has processed data
        mutex.lock();
        prev = idx;
        mutex.unlock();
        return 0;
      }
      sd.v_databuffer[idx].unlock(); // buffer is unavailable, release lock
    }
    usleep(10000);
    if(flag) return 1;
  }
}

// updater thread, picks processed buffers and saves results
// in staging database
void updater(int thread, SharedData& sd, Parameters& parameters) {
  pthread_setname_np(pthread_self(),"qdda-updater");
  int idx=0;
  while(true) {
    if(getProcessedBuffer(sd,idx)) break; // get processed buffer or end loop if processing is complete
    if(g_abort) return;
    sd.p_sdb->begin();    
    for(int j=0; j<sd.v_databuffer[idx].used; j++)
      sd.p_sdb->insertdata(sd.v_databuffer[idx].v_hash[j],sd.v_databuffer[idx].v_bytes[j]);
    sd.p_sdb->end();
    sd.v_databuffer[idx].reset();
    sd.v_databuffer[idx].status=0;
    sd.v_databuffer[idx].unlock();
  }
}

// Request a filled buffer - lock it and return the index
// or spinwait forever until one is available or if the readers are done is completed
int getUsedBuffer(SharedData& sd, int& index) {
  while(true) {
    bool flag = sd.read_completed; // readers are done, walk through buffers once more
    for(index=0; index < sd.size(); index++) {
      if(sd.v_databuffer[index].trylock()) continue; // in use
      if(sd.v_databuffer[index].status==1) return 0; // lock ack'ed and buffer is not empty
      sd.v_databuffer[index].unlock();               // buffer is empty, release lock
    }
    usleep(10000);     // no available buffers, sleep
    if(flag) return 1; // readers were done and we handled all remaining data
  }
}

// read a stream(file) into available buffers
ulong readstream(int thread, SharedData& shared, FileData& fd) {
  ulong rbytes,blocks;
  ulong totbytes=0;
  const ulong blocksize = shared.blocksize;
  int i;
  while(!fd.ifs->eof()) {
    getFreeBuffer(shared,i);
    if(g_abort) return 0;
    shared.throttle.request(shared.blockspercycle * blocksize); // IO throttling, sleep if we are going too fast
    shared.v_databuffer[i].offset = totbytes; //fd.ifs->tellg()/blocksize/1024;
    shared.v_databuffer[i].status = 1;
    fd.ifs->read(shared.v_databuffer[i].readbuf, blocksize*1024*shared.blockspercycle);
    rbytes     = fd.ifs->gcount();
    blocks     = rbytes / blocksize / 1024; // amount of full blocks
    blocks    += rbytes%blocksize*1024?1:0; // partial block read, add 1
    totbytes  += rbytes;
    shared.v_databuffer[i].used = blocks;
    shared.v_databuffer[i].unlock();
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
  int idx=0;
  ulong hash,bytes;
  while (true) {
    if(getUsedBuffer(sd,idx)) break; // get filled buffer or end loop if processing is complete
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
    sd.v_databuffer[idx].status = 2;
    sd.v_databuffer[idx].unlock();
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

  thread updater_thread;
  thread reader_thread[readers];
  thread worker_thread[workers];
  Stopwatch stopwatch;

  updater_thread = thread(updater,0, std::ref(sd), std::ref(parameters));
  for(int i=0; i<workers; i++) worker_thread[i] = thread(worker, i, std::ref(sd), std::ref(parameters));
  for(int i=0; i<readers; i++) reader_thread[i] = thread(reader, i, std::ref(sd), std::ref(filelist));

  for(int i=0; i<readers; i++) reader_thread[i].join();
  sd.read_completed = true; //signal workers that reading is complete
  for(int i=0; i<workers; i++) worker_thread[i].join();
  sd.work_completed = true; //signal updater that work is complete
  updater_thread.join();

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

