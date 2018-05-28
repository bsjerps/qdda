/*******************************************************************************
 * Title       : error.h
 * Description : Exception error handling
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : http://outrun.nl/wiki/qdda
 ******************************************************************************/

#pragma once

/*******************************************************************************
 * Exception/error handling
 * 
 * Call: throw Fatal(__LINENO__, __FILE__) << <your ostream parameters>
 *       throw Fatal(__LINENO__, __FILE__, "Error desc") << <parameters>;
 * 
 * Or using ERROR macro
 * throw ERROR("Error message");
 * throw ERROR("Error message") << more information;
 * 
 * In try/catch block:
 * try { whatever }
 * catch (Fatal& e) { e.print(); return <int>; }
 * 
 * Requires a global variable g_debug which, if enabled, prints the 
 * filename and linenumber of the c++ source where throw was called.
 * 
 * 
 * 
 ******************************************************************************/

extern bool g_debug;

#define ERROR(...) Fatal(__LINE__,__FILE__, ##__VA_ARGS__)
#define dumpvar(v)         std::cout << #v << "=" << v << std::endl << std::flush


class Fatal: public std::exception {
public:
  virtual ~Fatal() throw () {};
  Fatal(int l, const char * f)                {  _line = l; _file = f; _what = ""; }
  Fatal(int l, const char * f, const char* w) {  _line = l; _file = f; _what = w;  }
  Fatal(const Fatal& that) { _line = that._line; _file = that._file; _what = that._what; _msg << that._msg.str(); };
  
  virtual const char * what () const throw ()  { return _what; }
  void print() { print(std::cerr); }
  void print(std::ostream& os) {
    os << "Error";
    if (g_debug) os << " (" << _file << ", line " << _line << ")";
    os << ": " << _what << _msg.str() << std::endl;
  };
  template<typename T>
  Fatal& operator<<( const T& t ) {
    _msg << t;
    return *this;
  }
private:
  int         _line;
  const char* _file;
  const char* _what;
  mutable std::stringstream _msg;
};



