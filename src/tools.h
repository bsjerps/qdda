/*******************************************************************************
 * Title       : tools.h
 * Description : Various generic C++ functions and tools
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : http://outrun.nl/wiki/qdda
 ******************************************************************************/

#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <sstream>
#include <ostream>
#include <string.h>

#include "error.h"

struct option;

/*******************************************************************************
 * Debugging and messaging
 ******************************************************************************/

// For converting -DVERSION=x.y.z to string
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#ifdef __DEBUG
void dumpvars_exp(const char* names, int, ...);
void debugMsg(const char*, int); // show line and filename

#define WHERE              debugMsg(__FILE__,__LINE__);
#define dumpline           std::cout << __LINE__ << " ";
// show one or up to three variable names and values
#define dvar(v)            std::cout << #v << "=" << v << " "
#define dumpvar(v)         std::cout << #v << "=" << v << std::endl << std::flush
#define dumpvar2(p1,p2)    dvar(p1); dvar(p2); std::cout << std::endl << std::flush
#define dumpvar3(p1,p2,p3) dvar(p1); dvar(p2); dvar(p3); std::cout << std::endl << std::flush

#define RELEASE "Beta"
#else
#define RELEASE ""
#endif

// int64 and uint64 are the basic type for qdda as it matches 64 bit integers of SQLite
typedef uint64_t uint64;
typedef int64_t  int64;

/*******************************************************************************
 * String functions
 ******************************************************************************/

bool isNum(const std::string& s); // true if string only has digits
void toUpper(char *str);          // convert a string to uppercase (inplace)
void toUpper(std::string& str);   // same with string instead of char*
void searchReplace(std::string& src, std::string const& find, std::string const& repl); // global inplace search/replace within string

// Print to string with precision N (float, double)
template <typename T> std::string toString(const T value, const int n = 2) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(n) << value;
  return out.str();
}

/*******************************************************************************
 * Calculations
 ******************************************************************************/

inline int    maxint(int x, int y)            { return x>y?x:y; };
inline int    minint(int x, int y)            { return x<y?x:y; };
inline float  safeDiv_float(float x, float y) { return (y==0) ? 0 : x / y; }  // safe divide (just returns 0 if div by zero)
inline int64  safeDiv_int64(int64 x, int64 y) { return (y==0) ? 0 : x / y; }  // same for int64
inline int64  divRoundUp(int64 x, int64 y)    { return (x%y) ? x/y+1 : x/y; } // long integer division but round up instead of round down
inline uint64 longRand()                      { return ((uint64)rand() << 32) + rand() ; }  // 64-bit random unsigned int

/*******************************************************************************
 * System related info
 ******************************************************************************/

int   cpuCount();                                    // return number of cpus (cores)
int64 epoch();                                       // secs since 1970
const char* hostName();                              // system hostname
const char* whoAmI();                                // path to self
const std::string& homeDir();                        // User's home directory
const std::string  dirName(const std::string& file); // return dir portion of filename, including '/'
int   fileExists(const char * fn);                   // return true if file exists
long  fileSystemFree(const char* filename);          // filesystem free in MB for this file
off_t fileSize(const char *filename);                // return size of file in bytes

void armTrap();   // interrupt handler, enable
void resetTrap(); // disable

/*******************************************************************************
 * Stopwatch - a timer class that keeps track of time in microseconds
 ******************************************************************************/

class Stopwatch {
  std::chrono::high_resolution_clock::time_point t1,t2;
  const int64 diff() const;
public:
  Stopwatch()       { reset();}
  void reset()      { t1 = std::chrono::high_resolution_clock::now(); t2=t1;}          // reset timers
  const int64 lap() { t2 = std::chrono::high_resolution_clock::now(); return diff(); } // save laptime and return diff
  operator int64()  { return diff(); } // returns saved laptime
  const std::string seconds() const;   // return seconds in #.## format
  const std::string runtime() const;   // return runtime message
};

/*******************************************************************************
 * BoundedRange - a value guaranteed to be within min and max limits
 ******************************************************************************/

// throw this if we get a range error
class BoundedRange: public std::exception { };

// Boundedval<int, 5, 10> is guaranteed to contain an int between 5 and 10
template<typename T, int Tmin, int Tmax> class BoundedVal {
public:
  BoundedVal() { val = Tmin ; }
  operator T() { return val; }
  int operator=(T in) {
    if(in < Tmin || in > Tmax) throw BoundedRange();
    val = in ; 
  }
private:
  T val;
};

/*******************************************************************************
 * SmartVector - a vector template with some additional operators & functions
 * Not complete yet - work in progress
 ******************************************************************************/

template<typename T>
class SmartVector {
public:
  void clear()                        { v.clear() ; }
  void resize(size_t& s)              { v.resize(s); }
  size_t size() const                 { return v.size(); }
  T& operator+=(const T& i)           { v.push_back(i); return *this; }
  SmartVector& operator<<(const T& i) { v.push_back(i); return *this; }
  void print(std::ostream& os)        { for(int i=0; i<v.size(); i++) os << v[i] << std::endl; }
  const T& operator[](int i) const {
    if(i>=v.size()) throw ERROR("SmartVector: Index out of range");
    return v[i];
  };
private:
  std::vector<T> v;
};

typedef SmartVector<int>         IntArray;
typedef SmartVector<std::string> StringArray;

/*******************************************************************************
 * LongOptions - class that holds options for arguments processing
 * The option list can be built by adding option items
 * each option item modifies a variable or executes a void() function
 * It can print the options in help style or as a man page section
 ******************************************************************************/

class LongOptions {
  struct Option {
    const char*  name;    // long option name (--name)
    int          val;     // unique option value (identifier)
    const char*  optdesc; // description of option parameters
    const char*  desc;    // help description
    // option parameters (only one can be used, rest is 0/NULL)
    int*         p_int;   // pointer to parameter (int)
    int64*       p_int64; // pointer to parameter (unsigned long)
    bool*        p_bool;  // pointer to parameter (bool)
    std::string* p_str;   // pointer to parameter (string)
    void (*func)();       // pointer to void function with no parameters
  };
public:
  LongOptions();
  ~LongOptions();
  void  add(const char* name, char c, const char* p, bool&        v, const char* desc);
  void  add(const char* name, char c, const char* p, int&         v, const char* desc);
  void  add(const char* name, char c, const char* p, int64&       v, const char* desc);
  void  add(const char* name, char c, const char* p, std::string& v, const char* desc);
  void  add(const char* name, char c, const char* p, void (*f)(),    const char* desc);
  int   hasarg(int i) { return strlen(opts[i].optdesc)?1:0; }
  void  printhelp(std::ostream& os);
  void  printman(std::ostream& os);
  int   parse(int argc, char** argv);
private:
  int val;
  std::vector<Option> opts;
  option* p_longopts;
  void longopts_init();
};

