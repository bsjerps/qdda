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

#include <stdlib.h> // rand

#include <string.h>

struct option;

// dirty hack to improve readability
#define string std::string

/*******************************************************************************
 * Debugging and messaging
 ******************************************************************************/

// For converting -DVERSION=x.y.z to string
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#ifdef __DEBUG
void dumpvars_exp(const char* names, int, ...);
void debugMsg(const char*, int); // show line and filename

#define debug              debugMsg(__FILE__,__LINE__);
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

// Spartan error handling, just quit with error if something goes wrong
void die(string errMsg, int rc=10);

/*******************************************************************************
 * String functions
 ******************************************************************************/

bool isNum(const string& s);    // true if string only has digits
void toUpper(char *str);        // convert a string to uppercase (inplace)
void toUpper(string& str);      // same with string instead of char*
void searchReplace(string& src, string const& find, string const& repl); // global inplace search/replace within string

// Print to string with precision N (float, double)
template <typename T> string toString(const T value, const int n = 2) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(n) << value;
  return out.str();
}

/*******************************************************************************
 * Calculations
 ******************************************************************************/

inline ulong square(ulong x)                 { return x * x; };
inline int   maxint(int x, int y)            { return x>y?x:y; };
inline int   minint(int x, int y)            { return x<y?x:y; };
inline float safeDiv_float(float x, float y) { return (y==0) ? 0 : x / y; }  // safe divide (just returns 0 if div by zero)
inline ulong safeDiv_ulong(ulong x, ulong y) { return (y==0) ? 0 : x / y; }  // same for ulong
inline ulong divRoundUp(ulong x, ulong y)    { return (x%y) ? x/y+1 : x/y; } // long integer division but round up instead of round down
inline uint64_t longRand()                   { return ((uint64_t)rand() << 32) + rand() ; }  // 64-bit ulong random


/*******************************************************************************
 * System related info
 ******************************************************************************/

int   cpuCount();        // return number of cpus (cores)
ulong epoch();           // secs since 1970
const char* hostName();  // system hostname
const char* whoAmI();    // path to self
const string& homeDir();
const string  dirName(const string& file);   // return dir portion of filename, including '/'
int   fileExists(const char * fn);           // return true if file exists
long  fileSystemFree(const char* filename);  // filesystem free in MB for this file
off_t fileSize(const char *filename);        // return size of file in bytes

void armTrap();   // interrupt handler, enable
void resetTrap(); // disable

/*******************************************************************************
 * Stopwatch - a timer class that keeps track of time in microseconds
 ******************************************************************************/

class Stopwatch {
  std::chrono::high_resolution_clock::time_point t1,t2;
  const ulong diff() const;
public:
  Stopwatch()       { reset();}
  void reset()      { t1 = std::chrono::high_resolution_clock::now(); t2=t1;}          // reset timers
  const ulong lap() { t2 = std::chrono::high_resolution_clock::now(); return diff(); } // save laptime and return diff
  const string seconds() const;        // return seconds in #.## format
  const string runtime() const;        // return runtime message
  operator ulong()  { return diff(); } // returns saved laptime
};

/*******************************************************************************
 * StringArray - holds a variable number of strings (using std::vector)
 ******************************************************************************/

class StringArray {
  std::vector<string> v_string;
public:
  int size()    { return v_string.size(); }
  StringArray& operator+=(const string& s);
  StringArray& operator+=(const ulong);
  const string& operator[](int i);
  friend std::ostream& operator<< (std::ostream&, StringArray&);
};

/*******************************************************************************
 * LongOptions - class that holds options for arguments processing
 * The option list can be built by adding option items
 * each option item modifies a variable or executes a void() function
 * It can print the options in help style or as a man page section
 ******************************************************************************/

class LongOptions {
  struct Option {
    const char*  name;
    int          val;
    const char*  optname;
    const char*  desc;
    int*         p_int;
    ulong*       p_ulong;
    bool*        p_bool;
    string* p_str;
    void (*func)();
  };
public:
  LongOptions() { val = 1024; };
  // each add function takes a variable, 
  void  add(const char* name, char c, const char* p, bool&   v,   const char* desc);
  void  add(const char* name, char c, const char* p, int&    v,   const char* desc);
  void  add(const char* name, char c, const char* p, ulong&  v,   const char* desc);
  void  add(const char* name, char c, const char* p, string& v,   const char* desc);
  void  add(const char* name, char c, const char* p, void (*f)(), const char* desc);
  int   hasarg(int i) { return strlen(opts[i].optname)?1:0; }
  void  printhelp(std::ostream& os);
  void  printman(std::ostream& os);
  const option* long_opts();
  int   parse(int argc, char** argv);
private:
  int val;
  std::vector<Option> opts;
};

#undef string
