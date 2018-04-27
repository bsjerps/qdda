#============================================================================
# Title       : Makefile
# Description : Makefile for qdda (quick & dirty dedupe analyzer)
# Author      : Bart Sjerps <bart@outrun.nl>
# License     : GPLv3+
# URL         : http://outrun.nl/wiki/qdda
# ---------------------------------------------------------------------------

prefix     = /usr/local
mandir     = $(prefix)/share/man
datadir    = $(prefix)/share
RPM       := $(shell rpm --version 2>/dev/null)

version    = $(shell awk '/^Version:/ {print $$NF}' ../qdda.spec)
CXXFLAGS  += -std=c++0x -DVERSION=$(version)
CFLAGS    += -O3
LIBS       = -lpthread -lstdc++ -ldl
OBJECTS    = sqlite/sqlite3.o
OBJECTS   += md5/md5.o
OBJECTS   += lz4/lz4.o

# disable for prod version
# CXXFLAGS  += -D__DEBUG

all: qdda

qdda: qdda.o database.o tools.o output.o threads.o helptext.o $(OBJECTS)
	g++ $(LDFLAGS) qdda.o database.o tools.o helptext.o threads.o output.o $(OBJECTS) $(LIBS) -o qdda 

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

checkrpm:
ifndef RPM
	@echo RPM not installed, skipping src
	@exit 10
endif

# Create the archive for rpmbuild
src:	checkrpm allclean
	tar -jcf $$(rpm --eval %_sourcedir)/qdda-$(version).tbz2 --transform "s|^|qdda/|" *

# Create zipfile for download
zip:	all
	zip ../download/qdda-$(version).zip qdda

# Build the RPM from archive made in 'src' section
rpm:	checkrpm src
	cd .. ; rpmbuild -ba qdda.spec

allclean:
	rm -rf *.o */*.o qdda

install: qdda
	install -m 0755 -d $(prefix)/bin
	install -m 0755 -d $(mandir)/man1
	install -m 0755 -d $(datadir)/qdda
	install -m 0755 qdda $(prefix)/bin
	cp share/* $(datadir)/qdda
	$(prefix)/bin/qdda --mandump >$(mandir)/man1/qdda.1

tag:
	git tag v$(version)

.PHONY: install

