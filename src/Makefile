#============================================================================
# Title       : Makefile
# Description : Makefile for qdda (quick & dirty dedupe analyzer)
# Author      : Bart Sjerps <bart@outrun.nl>
# License     : GPLv3+
# URL         : http://outrun.nl/wiki/qdda
# ---------------------------------------------------------------------------

prefix     = /usr/local
sysconfdir = /etc

CXXFLAGS  += -std=c++0x
CFLAGS    += -O3
LIBS       = -lpthread -lstdc++ -ldl
# STATIC     = lib/liblz4.a

# disable for prod version
CXXFLAGS  += -D__DEBUG

all: qdda

# Static linking of lz4 avoids having to install dependencies
# The other libs are usually available on most Linux systems
# Note that building on newer systems causes problems with
# older versions of LIBC and STDC++ libraries.
# LZ4 needs to be version 1.31 (or higher).

qdda: qdda.o database.o tools.o output.o threads.o helptext.o contrib/sqlite3.o contrib/md5.o contrib/lz4.o
	g++ $(LDFLAGS) qdda.o contrib/md5.o database.o tools.o helptext.o threads.o output.o contrib/sqlite3.o contrib/lz4.o $(LIBS) $(STATIC) -o qdda 

qdda.o: qdda.cpp tools.h qdda.h database.h
	g++ -c $(CXXFLAGS) $(CFLAGS) qdda.cpp

database.o: database.cpp tools.h qdda.h database.h
	g++ -c $(CXXFLAGS) $(CFLAGS) database.cpp

lz4.o: contrib/lz4.c contrib/lz4.h
	g++ -c $(CFLAGS) lz4.c

md5.o: contrib/md5.c contrib/md5.h
	g++ -c $(CFLAGS) md5.c

sqlite3.o: contrib/sqlite3.c contrib/sqlite3.h
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

allclean:
	rm -rf *.o */*.o */*.a qdda

install: qdda
	install -d 0755 $(prefix)/bin
	install -d 0755 $(sysconfdir)/bash_completion.d
	install -m 0755 qdda $(prefix)/bin
	install -m 0644 support/qdda.bash $(sysconfdir)/bash_completion.d

.PHONY: install

