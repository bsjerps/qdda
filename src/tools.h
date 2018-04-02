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
#include <fstream>
#include <iomanip>

struct option;

// Debugging, messaging
void dumpvars_exp(const char* names, int, ...);
void debugMsg(const char*, int); // show line and filename

#define where              debugMsg(__FILE__,__LINE__);
#define dumpline           std::cout << __LINE__ << " ";
// show one or up to three variable names and values
#define dvar(v)            std::cout << #v << "=" << v << " "
#define dumpvar(v)         std::cout << #v << "=" << v << std::endl << std::flush
#define dumpvar2(p1,p2)    dvar(p1); dvar(p2); std::cout << std::endl << std::flush
#define dumpvar3(p1,p2,p3) dvar(p1); dvar(p2); dvar(p3); std::cout << std::endl << std::flush

// Spartan error handling, just quit with error if something goes wrong
void die(std::string errMsg, int rc=10);

// string stuff
bool isNum(const std::string& s);    // true if string only has digits
void toUpper(char *str);             // convert a string to uppercase (inplace)
void toUpper(std::string& str);      // same with string instead of char*
void searchReplace(std::string& src, std::string const& find, std::string const& repl); // global inplace search/replace within string

// Print to string with precision N (float, double)
template <typename T> std::string toString(const T value, const int n = 2) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(n) << value;
  return out.str();
}

// file stuff
int fileExists(const char * fn);                        // return true if file exists
std::ifstream::pos_type fileSize(const char* filename); // return size of file in bytes

// calc stuff
inline ulong square(ulong x)                 { return x * x; };
inline int   maxint(int x, int y)            { return x>y?x:y; };
inline int   minint(int x, int y)            { return x<y?x:y; };
inline float safeDiv_float(float x, float y) { return (y==0) ? 0 : x / y; }  // safe divide (just returns 0 if div by zero)
inline ulong safeDiv_ulong(ulong x, ulong y) { return (y==0) ? 0 : x / y; }  // same for ulong
inline ulong divRoundUp(ulong x, ulong y)    { return (x%y) ? x/y+1 : x/y; } // long integer division but round up instead of round down
inline ulong longRand()                      { return ((ulong)rand() << 32) + rand() ; }  // 64-bit ulong random

// System info
int   cpuCount();       // return number of cpus (cores)
ulong epoch();          // secs since 1970
const char* hostName(); // system hostname
const char* whoAmI();   // path to self
const std::string& homeDir(); 


// stopwatch class in microseconds - keep track of processing time
class Stopwatch {
  std::chrono::high_resolution_clock::time_point t1,t2;
  const ulong diff() const;
public:
  Stopwatch()       { reset();}
  void reset()      { t1 = std::chrono::high_resolution_clock::now(); t2=t1;}          // reset timers
  const ulong lap() { t2 = std::chrono::high_resolution_clock::now(); return diff(); } // save laptime and return diff
  const std::string seconds() const;                                                   // return seconds in #.## format
  const std::string runtime() const;                                                   // return runtime message
  operator ulong()  { return diff(); }                                                 // returns saved laptime
};

// Array of strings, dynamic allocation
class StringArray {
  std::vector<std::string> v_string;
public:
  int size()    { return v_string.size(); }
  StringArray& operator+=(const std::string& s);
  StringArray& operator+=(const ulong);
  const std::string& operator[](int i);
  friend std::ostream& operator<< (std::ostream&, StringArray&);
};

// A class that holds options for arguments processing
class LongOptions {
  struct Option {
    const char*  name;
    int          val;
    const char*  optname;
    const char*  desc;
    int*         p_int;
    ulong*       p_ulong;
    bool*        p_bool;
    std::string* p_str;
    void (*func)();
  };
public:
  LongOptions() { val = 1024; };
  void  add(bool& b,          int val, const char* name, const char* par, const char* desc);
  void  add(int& p,           int val, const char* name, const char* par, const char* desc);
  void  add(ulong& p,         int val, const char* name, const char* par, const char* desc);
  void  add(std::string& str, int val, const char* name, const char* par, const char* desc);
  void  add(void (*f)(),      int val, const char* name, const char* par, const char* desc);
  int   hasarg(int i) { return strlen(opts[i].optname)?1:0; }
  void  printhelp(std::ostream& os);
  void  printman(std::ostream& os);
  void  help();
  const option* long_opts();
  int   parse(int argc, char** argv);
private:
  int val;
  std::vector<Option> opts;
};

