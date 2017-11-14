/*******************************************************************************
 * Title       : tools.h
 * Description : Various generic C++ functions and tools
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : http://outrun.nl/wiki/qdda
 ******************************************************************************/

#pragma once
// Debugging, messaging
#define debug      std::cout << __FILE__ <<", line " << __LINE__ << std::endl;
#define dumpvar(v) std::cout << #v << "=" << v << std::endl;
void die(std::string errmsg,int rc=10);

// string stuff
void uppercase(char *str);             // convert a string to uppercase (inplace)
void uppercase(std::string& str);      // same with string instead of char*
void searchreplace(std::string& src, std::string const& find, std::string const& repl); // global s/r within string

// Print to string with precision N (float, double)
template <typename T> std::string to_string(const T a_value, const int n = 2) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(n) << a_value;
    return out.str();
}

// file stuff
int file_exists(const char * fn); // return true (1) if file exists

// calc stuff
inline float safediv_float(float x, float y) { return (y==0) ? 0 : x / y; }  // safe divide (just returns 0 if div by zero)
inline ulong safediv_ulong(ulong x, ulong y) { return (y==0) ? 0 : x / y; }  // same for ulong
inline ulong divup(ulong x, ulong y)         { return (x%y) ? x/y+1 : x/y; } // long integer division but round up instead of round down
inline ulong lrand() { return ((ulong)rand() << 32) + rand() ; }             // return ulong random, between 0 and 18446744073709551615UL;

// various

void os_reset(std::ostream& os = std::cout); // reset manipulators of an ostream (copy default flags from null)

// stopwatch class in microseconds

class stopwatch_t {
  std::chrono::high_resolution_clock::time_point t1,t2;
  const ulong diff() const;
public:
  operator ulong()  { return diff(); } // returns saved laptime
  stopwatch_t()     { reset();}
  void reset()      { t1 = std::chrono::high_resolution_clock::now(); t2=t1;}          // reset timers
  const ulong lap() { t2 = std::chrono::high_resolution_clock::now(); return diff(); } // save laptime and return diff
  const std::string seconds() const;                                                   // return seconds in #.## format
  const std::string runtime() const;                                                   // return runtime message
};
