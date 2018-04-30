/*******************************************************************************
 * Title       : threads.h
 * Description : header file for qdda - threads definitions
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : http://outrun.nl/wiki/qdda
 ******************************************************************************/

#pragma once
#include <vector>

long threadpid();

// simple Mutex class
class Mutex {
public:
  Mutex() {
    pthread_mutexattr_init (&attr);
    pthread_mutexattr_setrobust_np (&attr, PTHREAD_MUTEX_ROBUST_NP);
    pthread_mutex_init (&mutex, &attr);
  }
  ~Mutex()      { pthread_mutex_destroy(&mutex);}
  void lock()   { pthread_mutex_lock(&mutex);   }
  void unlock() { pthread_mutex_unlock(&mutex); }
  int trylock() { return pthread_mutex_trylock(&mutex); }
private:
  pthread_mutex_t mutex;
  pthread_mutexattr_t attr;
};

// Blockdata holds <blockspercycle> blocks of data from a stream read by the reader
// to be processed by the worker
class DataBuffer {
public:
  explicit DataBuffer(ulong, ulong);
 ~DataBuffer();
  void  reset();           // clear buffer and temp counters;
  char* operator[](int n); // access to the nth block in the buffer
  size_t size;
  ulong bufsize;           // size of the read buffer in bytes
  int   used;              // number of used blocks in the buffer
  ulong blocks;            // blocks read
  ulong bytes;             // bytes read
  char* readbuf;           // the actual data
  std::vector<ulong> v_hash;
  std::vector<ulong> v_bytes;
  ulong blocksize_bytes;
private:
  DataBuffer() = delete;
};

// IOThrottle holds shared data to provide IO bandwidth throttling
class IOThrottle {
public:
  explicit IOThrottle(ulong mibps);  // Initialize with megabytes per sec value
  void request(ulong kb);   // request to read a number of 1k blocks
private:
  ulong      mibps;         // the bandwidth
  Stopwatch  stopwatch;     // track time between requests
  std::mutex mx_throttle;   // thread safe
};

class RingBuffer {
public:
  explicit RingBuffer(size_t sz);
  int getfree(size_t& ix);
  int getfull(size_t& ix);
  int getused(size_t& ix);
  void release(size_t ix);
  bool done;
  size_t getsize() { return size;}
  void print();
private:
  bool isFull();
  bool hasData();
  bool isEmpty();
  bool isDone();
  std::mutex mx_meta;
  std::mutex tailbusy;
  std::mutex headbusy;
  std::mutex workbusy;
//  std::mutex mx_print;
  std::vector<Mutex> mx_buffer;
  size_t head;
  size_t tail;
  size_t work;
  size_t size;
};

// Shared data for easy sharing between threads
class SharedData {
public:
  SharedData(int buffers, int files, ulong blocksize, StagingDB*, int mibps);
 ~SharedData();
  std::vector<DataBuffer> v_databuffer;
  RingBuffer              rb;
  ulong                   blocksize;
  ulong                   blocks, bytes;
  ulong                   cbytes;
  StagingDB*              p_sdb;
  IOThrottle              throttle;
  int                     blockspercycle;
  Mutex*                  filelocks;
  std::mutex              mx_shared;
  std::mutex              mx_database;
};
