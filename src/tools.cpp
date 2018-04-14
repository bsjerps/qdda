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
#include <iomanip>

#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <sys/statvfs.h>
#include <pwd.h>
#include <getopt.h>
#include <signal.h>

#include "tools.h"

#define string std::string

extern bool g_abort;

// Die with error message
void die(string errmsg,int rc) { 
  std::cerr << "Error: " << errmsg << " " << std::endl << std::flush;
  exit(rc);
}

// show line and filename (using #define debug macro)
void debugMsg(const char* file, int line) {
  std::cout << file <<", line " << line << "\t" << std::endl << std::flush;
}

// convert a string to uppercase (inplace)
void toUpper(char *s) {
  while ( *s != '\0' ) {
    *s = toupper((unsigned char)*s);
    ++s;
  }
}

// Same for std::string
void toUpper(string& str) {
  char buf[str.size()];
  strcpy(buf,str.c_str());
  toUpper(buf);
  str = buf;
}

// Test if file exists (and readable?)
int fileExists(const char * fn) {
  std::ifstream f(fn);
  if(!f.fail()) return 1;
  return 0;
}

off_t fileSize(const char *filename) {
  struct stat st; 
  if (stat(filename, &st) == 0)
    return st.st_size;
  return -1; 
}

// stopwatch - get lap time
const ulong Stopwatch::diff() const {
  return std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
}

// stopwatch - show time as seconds, format #.##
const string Stopwatch::seconds() const {
  return toString((float)diff()/1000000,2);
}

// stopwatch - show time in microsec
const string Stopwatch::runtime() const {
  std:: stringstream ss;
  ss << "runtime " << diff() << " microsec (" << seconds() << " s)";
  return ss.str();
}

// search and replace within string
void searchReplace(string& source, string const& find, string const& replace) {
  for(string::size_type i = 0; (i = source.find(find, i)) != string::npos;) {
    source.replace(i, find.length(), replace);
    i += replace.length();
  }
}

// seconds since 1-1-1970
ulong epoch() {
  return std::chrono::system_clock::now().time_since_epoch().count()/1000000000;
}

// Get the (short) hostname
const char* hostName() {
  static char hostname[HOST_NAME_MAX];
  gethostname(hostname, HOST_NAME_MAX);
  return hostname;
}

// Get path to self
const char* whoAmI() {
  static char buf[1024];
  memset(buf, 0, sizeof(buf));
  int rc = readlink("/proc/self/exe",buf,1024);
  return buf;
}

// get homedir from $HOME or from pw_dir if $HOME is invalid
const string& homeDir() {
  static string homedir;
  const char* envhome = getenv("HOME");
  const char* pwdhome = getpwuid(getuid())->pw_dir;
  if(!envhome) homedir = pwdhome;
  else if(strlen(envhome)>3) homedir = envhome;
  else homedir = pwdhome;
  return homedir;
}

const string dirName(const string& in) {
  string dir;
  dir = in.substr(0,in.find_last_of("/")) + "/";
  return dir;  
}

// get filesystem free in MiB
long fileSystemFree(const char* path) {
  struct statvfs stat;
  if (statvfs(path, &stat) != 0) return -1;
  return (stat.f_bsize * stat.f_bavail)/1048576;
}

// Stringarray functions

// Add a string to the array
StringArray& StringArray::operator+=(const string& s) {
  v_string.push_back(s);
  return *this;
}

// Add a string to the array (converted from ulong)
StringArray& StringArray::operator+=(const ulong var) {
  v_string.push_back(toString(var,0));
  return *this;
}

// Return element i in string
const string& StringArray::operator[](int i) {
  if(i>=v_string.size()) 
    die ("Index out of range for StringArray");
  return v_string[i];
}

// Dump the entire array to ostream
std::ostream& operator<<(std::ostream& stream, StringArray& s) {
  std::vector<string> v_string;
  for(std::vector<string>::iterator i = s.v_string.begin(); i != s.v_string.end(); i++)
    stream << *i << std::endl;
  return stream;
}

// Test if string is digits only
bool isNum(const string& s) {
  char* p;
  strtol(s.c_str(), &p, 10);
  return *p == 0;
}

// Get number of cpu cores
int cpuCount() {
  return sysconf(_SC_NPROCESSORS_ONLN);
}


/*******************************************************************************
 * LongOptions class functions
 ******************************************************************************/
  
void LongOptions::add(const char* name, char c, const char* p, bool&  v, const char* desc) {
  opts.push_back( (Option) { name, c?c:++val, p, desc } );
  opts.back().p_bool = &v;
}

void LongOptions::add(const char* name, char c, const char* p, int&   v, const char* desc) {
  opts.push_back( (Option) { name, c?c:++val, p, desc } );
  opts.back().p_int = &v;
}

void LongOptions::add(const char* name, char c, const char* p, ulong& v, const char* desc) {
  opts.push_back( (Option) { name, c?c:++val, p, desc } );
  opts.back().p_ulong = &v;
}

void LongOptions::add(const char* name, char c, const char* p, string& v, const char* desc) {
  opts.push_back( (Option) { name, c?c:++val, p, desc } );
  opts.back().p_str = &v;
}

void LongOptions::add(const char* name, char c, const char* p, void (*f)(), const char* desc) {
  opts.push_back( (Option) { name, c?c:++val, p, desc } );
  opts.back().func = f;
}

void LongOptions::printhelp(std::ostream& os) {
  for(int i=0;i<opts.size();i++) {
    string shortp, longp;
    if(opts[i].val<255) shortp = string("-") + string(1, opts[i].val) + ", ";
    longp = string("--") + opts[i].name + " " + opts[i].optname;
    os << std::setw(4) << std::left << shortp
       << std::setw(22) << std::left << longp
       << std::left << opts[i].desc
       << std::endl;
  }
}

void LongOptions::printman(std::ostream& os) {
  for(int i=0;i<opts.size();i++) {
    string shortp, longp;
    if(opts[i].val<255) shortp = string("-") + string(1, opts[i].val) + ", ";
    longp = string("--") + opts[i].name + " " + opts[i].optname;
    os << ".TP\n.B \\" << shortp << longp
       << "\n" << opts[i].desc << "\n";
  }
  os << ".P\n";
}

const option* LongOptions::long_opts() {
  option* o = new option[opts.size()+1];
  for(int i=0; i<opts.size(); i++) {
    option x = (option) { opts[i].name, hasarg(i), NULL, opts[i].val};
    o[i] = x;
  }
  o[opts.size()] = (option) { 0,0,0,0 };
  return o;
}

int LongOptions::parse(int argc, char** argv) {
  int c;
  string optstr = "+h";
  for(int i=0;i<opts.size();i++) {
    if(opts[i].val<255) {
      optstr += opts[i].val;
      if(strlen(opts[i].optname)) optstr+=":";
    }
  }
  while ((c = getopt_long(argc, argv, optstr.c_str(), long_opts(), NULL)) != -1) {
    for(int i=0;i<opts.size();i++) {
      if(c=='?') return 1;
      if(c==opts[i].val) {
        if(opts[i].func)    { opts[i].func(); exit(0); }
        if(opts[i].p_bool)  { *opts[i].p_bool = true; }
        if(!hasarg(i)) continue;
        if(opts[i].p_ulong) { *opts[i].p_ulong = atol(optarg); }
        if(opts[i].p_int)   { *opts[i].p_int = atol(optarg); }
        if(opts[i].p_str)   { *opts[i].p_str = optarg; }
        break;
      }
    }
  }
  return 0;
}

// Signal Handler for SIGINT (i.e. ctrl-c)
void setabort(int sig_num) {
  signal(SIGINT, setabort); // no rearm
  g_abort = true;  
  resetTrap();
}

void armTrap() {
  signal(SIGINT, setabort);
}

void resetTrap() {
  signal(SIGINT, SIG_DFL);
}
