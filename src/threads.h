/*******************************************************************************
 * Title       : threads.h
 * Description : header file for qdda - threads definitions
 * Author      : Bart Sjerps <bart@dirty-cache.com>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : https://wiki.dirty-cache.com/qdda
 ******************************************************************************/

#pragma once

typedef std::vector<uint64> v_uint64;

/*******************************************************************************
 * Functions
 ******************************************************************************/

long threadpid();

/*******************************************************************************
 * Mutex class
 * Note that we cannot always use std::mutex so we use a custom class
 ******************************************************************************/

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

/*******************************************************************************
 * Databuffer class - holds <blockspercycle> blocks of data from a stream
 * read by the reader to be processed by the worker
 ******************************************************************************/

class DataBuffer {
public:
  explicit DataBuffer(int64 blocksize, int64 blocksps);
 ~DataBuffer();
  void  reset();           // clear buffer and temp counters;
  char* operator[](int n); // access to the nth block in the buffer
  size_t blocks;           // size of buffer in blocks
  size_t bufsize;          // size of the read buffer in bytes
  int    used;             // number of used blocks in the buffer
  int64  blockcount;       // blocks read
  size_t bytes;            // bytes read
  char*  readbuf;          // the actual data
  v_uint64 v_hash;         // array of hashes
  v_uint64 v_bytes;        // array of compressed byte sizes
  uint64 blockbytes;       // blocksize in bytes
private:
  DataBuffer() = delete;
};

/*******************************************************************************
 * IOThrottle class - holds shared data to provide IO bandwidth throttling
 ******************************************************************************/

class IOThrottle {
public:
  explicit IOThrottle(int64 mibps);  // Initialize with megabytes per sec value
  void request(int64 kb);            // request to read a number of 1k blocks
private:
  uint64     mibps;                  // the bandwidth
  Stopwatch  stopwatch;              // track time between requests
  std::mutex mx_throttle;            // thread safe
};

/*******************************************************************************
 * Ringbuffer class - Keeps track of multiple buffers between threads
 * 
 * Implementation:
 *   +------------- tail (points to processed buffer for updater)
 *   |   +--------- work (points to buffer with data for worker)
 *   |   |     +--- head (points to free buffer for reader)
 *   |   |     |
 *   v   v     v
 * 0 1 2 3 4 5 6 7 8 9
 *               ^
 *               +-- head
 *               +-- work ( work == head means no work to do - wait for reader)
 * 
 * 0 1 2 3 4 5 6 7 8 9
 *       ^ ^
 *       | +-- work
 *       | +-- tail ( tail == work means no data - wait for worker)
 *       +-----head ( head == tail-1 means buffer is full - wait for updater)
 ******************************************************************************/

class RingBuffer {
public:
  explicit RingBuffer(size_t sz);  // create ringbuffer with sz buffers
  int getfree(size_t& ix);         // get ref to a free buffer (for reader)
  int getfull(size_t& ix);         // get ref to a full buffer (for worker)
  int getused(size_t& ix);         // get ref to a used buffer (for updater)
  void release(size_t ix);         // release the buffer (mark free)
  bool done;                       // true if all readers are done
  size_t getsize() { return size;}
  void print();                    // print pointers (debugging)
private:
  bool isFull();                // Test if ringbuffer is full
  bool hasData();               // Test if ringbuffer has unprocessed data
  bool isEmpty();               // Test if ringbuffer is empty
  bool isDone();                // Test if ringbuffer is done
  std::mutex mx_meta;           // mutex for updating RB metadata
  std::mutex tailbusy;          // mutex for updating tail
  std::mutex headbusy;          // mutex for updating head
  std::mutex workbusy;          // mutex for updating work
  std::vector<Mutex> mx_buffer; // mutexes for buffers in use by any thread
  size_t head;                  // head of RB
  size_t tail;                  // tail of RB
  size_t work;                  // middle of RB
  size_t size;                  // number of buffers
};

/*******************************************************************************
 * SharedData struct - for easy sharing between threads using only one parameter
 ******************************************************************************/

struct SharedData {
  SharedData(int buffers, int files, int64 blocksize, StagingDB*, int mibps);
 ~SharedData();
  std::vector<DataBuffer> v_databuffer;
  RingBuffer              rb;
  Blocksize               blocksize;
  Interval                interval;
  int                     method;
  int64                   blocks, bytes;
  int64                   cbytes;
  StagingDB*              p_sdb;
  IOThrottle              throttle;
  int64                   blockspercycle;
  Mutex*                  filelocks;
  std::mutex              mx_shared;
  std::mutex              mx_database;
};
