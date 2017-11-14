/*******************************************************************************
 * Title       : tools.cpp
 * Description : Various generic C++ functions and tools
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : http://outrun.nl/wiki/qdda
 ******************************************************************************/

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstring>
#include <chrono>

#include "tools.h"

// Die with error message - severe errors (should not happen)
void die(std::string errmsg,int rc) { 
  std::cerr << "Error: " << errmsg << " " << std::endl << std::flush;
  exit(rc);
}

// reset manipulators of an ostream (copy default flags from null)
void os_reset(std::ostream& os) { os.copyfmt(std::ios(NULL)); }

// convert a string to uppercase (inplace)
void uppercase(char *str) {
  while ( *str != '\0' ) {
    *str = toupper ( ( unsigned char ) *str );
    ++str;
  }
}

void uppercase(std::string& str) {
  char buf[str.size()];
  strcpy(buf,str.c_str());
  uppercase(buf);
  str = buf;
}

// test if file exists
int file_exists(const char * fn) {
  std::ifstream f(fn);
  if(!f.fail()) return 1;
  return 0;
}

// stopwatch - get lap time
const ulong stopwatch_t::diff() const {
  return std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
}

// stopwatch - show time as seconds, format #.##
const std::string stopwatch_t::seconds() const {
  return to_string((float)diff()/1000000,2);
}

const std::string stopwatch_t::runtime() const {
  std:: stringstream ss;
  ss << "runtime " << diff() << " microsec (" << seconds() << " s)";
  return ss.str();
}

// search and replace within string
void searchreplace(std::string& source, std::string const& find, std::string const& replace) {
  for(std::string::size_type i = 0; (i = source.find(find, i)) != std::string::npos;) {
    source.replace(i, find.length(), replace);
    i += replace.length();
  }
}
