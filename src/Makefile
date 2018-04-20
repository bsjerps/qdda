#============================================================================
# Title       : Makefile
# Description : Makefile for qdda (quick & dirty dedupe analyzer)
# Author      : Bart Sjerps <bart@outrun.nl>
# License     : GPLv3+
# URL         : http://outrun.nl/wiki/qdda
# ---------------------------------------------------------------------------

prefix     = /usr/local
version    = 2.0.0

CXXFLAGS  += -std=c++0x
CFLAGS    += -O3
LIBS       = -lpthread -lstdc++ -ldl
OBJECTS    = sqlite/sqlite3.o
OBJECTS   += md5/md5.o
OBJECTS   += lz4/lz4.o

# disable for prod version
# CXXFLAGS  += -D__DEBUG

all: qdda

qdda: qdda.o database.o tools.o output.o threads.o helptext.o $(OBJECTS)
	g++ $(LDFLAGS) qdda.o database.o tools.o helptext.o threads.o output.o $(OBJECTS) $(LIBS) $(STATIC) -o qdda 

qdda.o: qdda.cpp tools.h qdda.h database.h
	g++ -c $(CXXFLAGS) $(CFLAGS) qdda.cpp

database.o: database.cpp tools.h qdda.h database.h
	g++ -c $(CXXFLAGS) $(CFLAGS) database.cpp

lz4.o: lz4/lz4.c lz4/lz4.h
	g++ -c $(CFLAGS) lz4.c

md5.o: md5/md5.c md5/md5.h
	g++ -c $(CFLAGS) md5.c

sqlite3.o: sqlite/sqlite3.c sqlite/sqlite3.h
	g++ -c $(CFLAGS) sqlite3.c

tools.o: tools.cpp tools.h
	g++ -c $(CXXFLAGS) $(CFLAGS) tools.cpp

threads.o: threads.cpp tools.h database.h threads.h qdda.h
	g++ -c $(CXXFLAGS) $(CFLAGS) threads.cpp

output.o: output.cpp tools.h database.h
	g++ -c $(CXXFLAGS) $(CFLAGS) output.cpp

helptext.o: helptext.cpp
	g++ -c $(CXXFLAGS) $(CFLAGS) helptext.cpp

clean:
	rm -rf *.o qdda

src:
	tar -jcvf $(HOME)/qdda-$(version).tbz2 --transform "s|^|qdda/|" *

allclean:
	rm -rf *.o */*.o qdda

install: qdda
	install -d 0755 $(prefix)/bin
	install -m 0755 qdda $(prefix)/bin

.PHONY: install

