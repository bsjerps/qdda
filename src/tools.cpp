/*******************************************************************************
 * Title       : tools.cpp
 * Description : Various generic C++ functions and tools
 * Author      : Bart Sjerps <bart@dirty-cache.com>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : https://wiki.dirty-cache.com/qdda
 ******************************************************************************/

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <pwd.h>
#include <getopt.h>
#include <signal.h>

#include "error.h"
#include "tools.h"

using std::string;

sig_atomic_t g_abort; // signal other threads we've aborted
extern bool g_debug;  // debug flag

// show line and filename (using #define debug macro)
void debugMsg(const char* file, int line) {
  std::cout << file <<", line " << line << "\t" << std::endl << std::flush;
}

/*******************************************************************************
 * String functions
 ******************************************************************************/

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

// search and replace within string
void searchReplace(string& source, string const& find, string const& replace) {
  for(string::size_type i = 0; (i = source.find(find, i)) != string::npos;) {
    source.replace(i, find.length(), replace);
    i += replace.length();
  }
}

// Test if string is digits only
bool isNum(const string& s) {
  char* p;
  strtol(s.c_str(), &p, 10);
  return *p == 0;
}

/*******************************************************************************
 * System related
 ******************************************************************************/

// Test if file exists (and readable?)
int fileExists(const char * fn) {
  std::ifstream f(fn);
  if(!f.fail()) return 1;
  return 0;
}

// return filesize
off_t fileSize(const char *filename) {
  struct stat st; 
  if (stat(filename, &st) == 0)
    return st.st_size;
  return -1; 
}

// return dir part of filename
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

// seconds since 1-1-1970
int64 epoch() {
  return std::chrono::system_clock::now().time_since_epoch().count()/1000000000;
}

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif
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
  if(!readlink("/proc/self/exe",buf,1024)) throw ERROR("Cannot determine path to executable");
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

// Get number of cpu cores
int cpuCount() {
  return sysconf(_SC_NPROCESSORS_ONLN);
}

/*******************************************************************************
 * Stopwatch - a timer class that keeps track of time in microseconds
 ******************************************************************************/

// stopwatch - get lap time
const int64 Stopwatch::diff() const {
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

/*******************************************************************************
 * LongOptions class functions
 ******************************************************************************/

LongOptions::LongOptions() {
  val = 1024;
  p_longopts = NULL; 
}

LongOptions::~LongOptions() {
  if(p_longopts!=NULL) delete p_longopts;
}

// add a command line option, using various parameter types
// each add function adds an option to the list
// name: the (long) name of the parameter i.e. --name
// c:    if there is a shorthand (-x) then provide the ascii char, else 0
// p:    long option parameter ("" if none)
// var:  a variable of type bool|int|int64|string|function to be used with the option
// desc: short help description of the parameter

void LongOptions::add(const char* name, char c, const char* p, bool&  v, const char* desc) {
  opts.push_back( (Option) { name, c?c:++val, p, desc } );
  opts.back().p_bool = &v;
}

void LongOptions::add(const char* name, char c, const char* p, int&   v, const char* desc) {
  opts.push_back( (Option) { name, c?c:++val, p, desc } );
  opts.back().p_int = &v;
}

void LongOptions::add(const char* name, char c, const char* p, int64& v, const char* desc) {
  opts.push_back( (Option) { name, c?c:++val, p, desc } );
  opts.back().p_int64 = &v;
}

void LongOptions::add(const char* name, char c, const char* p, string& v, const char* desc) {
  opts.push_back( (Option) { name, c?c:++val, p, desc } );
  opts.back().p_str = &v;
}

void LongOptions::add(const char* name, char c, const char* p, void (*f)(), const char* desc) {
  opts.push_back( (Option) { name, c?c:++val, p, desc } );
  opts.back().func = f;
}

// Print short help (when using -h)
void LongOptions::printhelp(std::ostream& os) {
  for(int i=0;i<opts.size();i++) {
    string shortp, longp;
    if(opts[i].val<255) shortp = string("-") + string(1, opts[i].val) + ", ";
    longp = string("--") + opts[i].name + " " + opts[i].optdesc;
    os << std::setw(4) << std::left << shortp
       << std::setw(23) << std::left << longp
       << std::left << opts[i].desc
       << std::endl;
  }
}

// Print man page section (--mandump)
void LongOptions::printman(std::ostream& os) {
  for(int i=0;i<opts.size();i++) {
    string shortp, longp;
    if(opts[i].val<255) shortp = string("-") + string(1, opts[i].val) + ", ";
    longp = string("--") + opts[i].name + " " + opts[i].optdesc;
    os << ".TP\n.B \\" << shortp << longp
       << "\n" << opts[i].desc << "\n";
  }
  os << ".P\n";
}

// Create option* array for getopt_long from opts array
void LongOptions::longopts_init() {
  if(p_longopts!=NULL) return;
  p_longopts = new option[opts.size()+1];
  for(int i=0; i<opts.size(); i++) {
    option x = (option) { opts[i].name, hasarg(i), NULL, opts[i].val};
    p_longopts[i] = x;
  }
  p_longopts[opts.size()] = (option) { 0,0,0,0 };
}

// parse options
int LongOptions::parse(int argc, char** argv) {
  longopts_init();
  int c;
  // create opt string
  string optstr = "+h";
  for(int i=0;i<opts.size();i++) {
    if(opts[i].val<255) {
      optstr += opts[i].val;
      if(strlen(opts[i].optdesc)) optstr+=":";
    }
  }
  // walk through arguments and process
  while ((c = getopt_long(argc, argv, optstr.c_str(), p_longopts, NULL)) != -1) {
    for(int i=0;i<opts.size();i++) {
      if(c=='?') throw ERROR("Invalid parameter");
      if(c==opts[i].val) {
        if(opts[i].func)    { opts[i].func(); return 1; }        // call void function
        if(opts[i].p_bool)  { *opts[i].p_bool = true; }          // set boolean
        if(!hasarg(i)) continue;                                 // no val -> do nothing
        if(opts[i].p_int64) { *opts[i].p_int64 = atol(optarg); } // set unsigned long
        if(opts[i].p_int)   { *opts[i].p_int = atol(optarg); }   // set int
        if(opts[i].p_str)   { *opts[i].p_str = optarg; }         // set string
        break;
      }
    }
  }
  return 0;
}

/*******************************************************************************
 * Signal handlers for SIGINT (i.e. ctrl-c)
 ******************************************************************************/

void setabort(int sig_num) {
  signal(SIGINT, setabort);
  g_abort = true;  
}

void armTrap() {
  signal(SIGINT, setabort);
}

void resetTrap() {
  signal(SIGINT, SIG_DFL);
}
