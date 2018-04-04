/*******************************************************************************
 * Title       : threads.h
 * Description : header file for qdda - threads definitions
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : http://outrun.nl/wiki/qdda
 ******************************************************************************/

#include <vector>

long threadpid();

class Mutex {
public:
  Mutex()       { pthread_mutex_init(&mutex,0); }
  ~Mutex()      { pthread_mutex_destroy(&mutex);}
  void lock()   { pthread_mutex_lock(&mutex);   }
  void unlock() { pthread_mutex_unlock(&mutex); }
  int trylock() { return pthread_mutex_trylock(&mutex); }
private:
  pthread_mutex_t mutex;
};

// Blockdata holds <blockspercycle> blocks of data from a stream read by the reader
// to be processed by the worker
class DataBuffer {
public:
  DataBuffer(ulong);
 ~DataBuffer();
  int   trylock();
  void  unlock();
  void  reset();           // clear buffer and temp counters;
  ulong bufsize;           // size of the read buffer in bytes
  int   used;              // number of read blocks in the structure
  ulong blocks;            // blocks read
  ulong bytes;             // bytes read
  char* readbuf;           // the actual data
  char* operator[](int n); // access to the nth buffer
private:
  ulong blocksize_kb;
  Mutex mutex;
};


// IOThrottle holds shared data to provide IO bandwidth throttling
class IOThrottle {
public:
  IOThrottle(ulong mibps);  // Initialize with megabytes per sec value
  void request(ulong kb);   // request to read a number of 1k blocks
  ulong mibps;              // the bandwidth
  Stopwatch stopwatch;      // track time between requests
  Mutex mutex;
};

// Shared data for easy sharing between threads
class SharedData {
public:
  SharedData(int buffers, int files, ulong blocksize, StagingDB*, int mibps);
 ~SharedData();
  int size();
  bool                    read_completed;
  std::vector<DataBuffer> v_databuffer;
  ulong                   blocksize;
  ulong                   blocks, bytes;
  ulong                   cbytes;
  ulong                   rsleeps,wsleeps;
  StagingDB*              p_sdb;
  IOThrottle              throttle;
  Mutex                   mutex_output;
  Mutex                   mutex_database;
  Mutex                   mutex_self;
  Mutex*                  a_mutex_files;
  int                     blockspercycle;
private:
  int                     bufsize;
};
