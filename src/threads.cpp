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

const ulong blockspercycle = 64; // read x blocks at a time

extern bool g_debug;
extern bool g_quiet;
//extern ofstream c_verbose;
//extern ofstream c_debug;

DataBuffer::DataBuffer(ulong blocksize) {
  blocksize_kb = blocksize * 1024;
  bufsize      = blocksize_kb * blockspercycle;
  readbuf      = new char[bufsize];
  used         = 0;
  blocks       = 0;
  bytes        = 0;
}

DataBuffer::~DataBuffer() {
  delete[] readbuf;
}

void DataBuffer::reset() {
  memset(readbuf,0,bufsize);
  used=0;
}

int DataBuffer::trylock() { return mutex.trylock(); } // return pthread_mutex_trylock(&mutex); }
void DataBuffer::unlock() { mutex.unlock(); }

// access to the nth buffer
char* DataBuffer::operator[](int n) {
  return readbuf + (n*blocksize_kb);
}


IOThrottle::IOThrottle(ulong m) {
  pthread_mutex_init(&mutex,0);
  mibps = m;
}
IOThrottle::~IOThrottle() {
  pthread_mutex_destroy(&mutex);
}

// Request to read x kb, microsleep to match required bandwidth
void IOThrottle::request(ulong blocks_kb) {
  if(!mibps) return; // immediately return if we don't throttle
  pthread_mutex_lock(&mutex);
  stopwatch.lap();
  ulong microsec = (1024 * blocks_kb) / mibps;
  if(microsec > stopwatch)
    usleep(microsec - stopwatch);
  stopwatch.reset();
  pthread_mutex_unlock(&mutex);
}

SharedData::SharedData(int buffers, int files, ulong blksz, StagingDB* db, int bw): 
  throttle(bw)
{
  bufsize        = buffers;
  blocksize      = blksz;
  read_completed = false;
  blocks         = 0;
  bytes          = 0;
  cbytes         = 0;
  rsleeps        = 0;
  wsleeps        = 0;
  p_sdb          = db;
  v_databuffer.reserve(bufsize);
  for(int i=0;i<bufsize;i++) {
    DataBuffer* d = new DataBuffer(blocksize);
    v_databuffer.push_back(*d);
  }
  a_mutex_files = new Mutex[files];
}

SharedData::~SharedData() {
  delete[] a_mutex_files;
}

int SharedData::size() { return bufsize; }

long threadPid() { return (long int)syscall(SYS_gettid); }

void printthread(pthread_mutex_t* p_mutex, string msg) {
  pthread_mutex_lock(p_mutex);
  if(g_debug) std::cerr << msg << std::endl << std::flush;
  pthread_mutex_unlock(p_mutex);
}

int getFreeBlock(SharedData& sd) {
  for(;;) {
    for(int i=0; i < sd.size() ; i++) {
      if(sd.v_databuffer[i].trylock()) continue; // in use
      if(sd.v_databuffer[i].used==0) return i;   // lock ack'ed and buffer is empty
      sd.v_databuffer[i].unlock();               // buffer is full, release lock
    }
    sd.mutex_self.lock();
    sd.rsleeps++;
    sd.mutex_self.unlock();
    usleep(10000);    // no unlocked buffers, sleep
  }
}

int getUsedBlock(SharedData& sd, int& idx) {
  int flag;
  for(;;) {
    flag=sd.read_completed;
    for(idx=0; idx < sd.size() ; idx++) {
      if(sd.v_databuffer[idx].trylock()) continue; // in use
      if(sd.v_databuffer[idx].used!=0) return 0;   // lock ack'ed and buffer is not empty
      sd.v_databuffer[idx].unlock();               // buffer is empty, release lock
    }
    usleep(10000);    // no unlocked buffers, sleep
    sd.mutex_self.lock();
    sd.wsleeps++;
    sd.mutex_self.unlock();
    if(flag) return 1;
  }
}

// read a stream(file) into available buffers
ulong readstream(int thread, SharedData& shared,  ifstream& ifs) {
  ulong rbytes,blocks;
  ulong totbytes=0;
  const ulong blocksize = shared.blocksize;
  int idx=0;
  while(!ifs.eof()) {
    idx=getFreeBlock(shared);
    shared.throttle.request(blocksize*blockspercycle);
    ifs.read(shared.v_databuffer[idx].readbuf, blocksize*1024*blockspercycle);
    rbytes     = ifs.gcount();
    blocks     = rbytes / blocksize / 1024; // full blocks
    blocks    += rbytes%blocksize*1024?1:0; // partial block, add 1
    totbytes  += rbytes;
    shared.v_databuffer[idx].used = blocks;
    shared.v_databuffer[idx].unlock();
  }
  return totbytes;
}

// reader thread, pick a file if available
// each reader handles 1 file at a time
void reader(int thread, SharedData& sd, Filelist& filelist) {
  pthread_setname_np(pthread_self(), "qdda-reader");
  int numfiles = filelist.size();
  ulong bytes,rc;
  for(int i=0; i<numfiles; i++) {
    rc=sd.a_mutex_files[i].trylock();
    if(rc==0) {
      bytes = readstream(thread, sd, filelist[i]);
      sd.p_sdb->savemeta(filelist.name(i), bytes/sd.blocksize/1024, bytes);
    }
    // deliberately refusing to unlock so files never get read multiple times
  }
  
}

void worker(int thread, SharedData& sd, Parameters& parameters) {
  pthread_setname_np(pthread_self(), "qdda-worker");
  const ulong blocksize = sd.blocksize;
  ulong* p_hash   = new ulong[blockspercycle];
  ulong* p_cbytes = new ulong[blockspercycle];
  char* dummy     = new char[blocksize*1024];
  int idx=0;
  for(;;) {
    if(getUsedBlock(sd,idx)) break;
    for(int i=0; i < sd.v_databuffer[idx].used; i++) {
      DataBuffer& r_blockdata = sd.v_databuffer[idx];
      p_hash[i]   = hash_md5(r_blockdata[i], dummy, blocksize*1024);
      p_cbytes[i] = p_hash[i] ? compress(r_blockdata[i], dummy, blocksize*1024) : 0;
      sd.v_databuffer[idx].blocks++;
      sd.v_databuffer[idx].bytes += blocksize*1024;
      sd.mutex_self.lock();
      sd.blocks++;
      sd.bytes += blocksize*1024;
      sd.mutex_self.unlock();
      sd.mutex_output.lock();
      if(sd.blocks%10000==0)
        progress(sd.blocks, blocksize, sd.bytes);           // progress indicator
      sd.mutex_output.unlock();
    }
    if(!parameters.dryrun) {
      sd.mutex_database.lock();
      sd.p_sdb->begin();
      for(int i=0; i<sd.v_databuffer[idx].used; i++)
        sd.p_sdb->insertkv(p_hash[i],p_cbytes[i]);   // store values in DB
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
 
void analyze(Filelist& filelist, QddaDB& db, Parameters& parameters) {
  if(g_debug) cout << "Main thread pid " << getpid() << endl;
  string tmpname = parameters.tmpdir + "/qdda-staging.db";
  Database::deletedb(tmpname);
  StagingDB::createdb(tmpname, db.getblocksize());
  StagingDB stagingdb(tmpname);

  int workers     = parameters.workers;
  int readers     = min(filelist.size(), parameters.readers);
  int buffers     = parameters.buffers ? parameters.buffers : workers + readers + 2;
  ulong blocksize = db.getblocksize();

  SharedData sd(buffers, filelist.size(), blocksize, &stagingdb, parameters.bandwidth);

  if(!g_quiet) cout << "Scanning " << filelist.size() << " files, " 
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
  sd.read_completed = true;
  for(int i=0; i<workers; i++) worker_thread[i].join();

  stopwatch.lap();
  stringstream ss;
  ss << " Scanned in " << stopwatch.seconds() << " seconds" << endl << flush;
  progress(sd.blocks, sd.blocksize, sd.bytes, ss.str().c_str());
  //cout << endl << flush;
  ulong sumblocks = 0;
  ulong sumbytes = 0;
  for(int i=0;i<sd.size();i++) {
    sumblocks += sd.v_databuffer[i].blocks;
    sumbytes  += sd.v_databuffer[i].bytes;
  }
  if(g_debug) cerr << "Blocks processed " << sumblocks 
            << ", bytes = " << sumbytes
            << " (" << fixed << setprecision(2) << sumbytes/1024.0/1024 << " MiB)" << endl;
}

