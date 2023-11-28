#============================================================================
# Title       : Makefile
# Description : Makefile for qdda (quick & dirty dedupe analyzer)
# Author      : Bart Sjerps <bart@dirty-cache.com>
# License     : GPLv3+
# URL         : https://wiki.dirty-cache.com/qdda
# ---------------------------------------------------------------------------
# qdda is designed to be built on Enterprise Linux 6 - gcc 4.4.7 - C++0x
#============================================================================

SPEC      := ../qdda.spec

RPM        = $(shell rpm --version 2>/dev/null)
NAME      := qdda
VERSION    = $(shell rpm -q --qf '%{version}\n' --specfile $(SPEC)|head -1)
SOURCES    = $(shell rpm --eval %_sourcedir)

CXXFLAGS  += -std=c++0x -DVERSION="\"$(version)\""
CFLAGS    += -O3
LIBS       = -lpthread -lstdc++ -ldl
OBJECTS    = sqlite/sqlite3.o
OBJECTS   += md5/md5.o
OBJECTS   += lz4/lz4.o
OBJECTS   += zlib/libz.a

# disable for prod version
# CXXFLAGS  += -D__DEBUG
# Debugger
# CXXFLAGS  += -ggdb

all: qdda

qdda: qdda.o database.o tools.o output.o threads.o helptext.o $(OBJECTS)
	g++ $(LDFLAGS) qdda.o database.o tools.o helptext.o threads.o output.o $(OBJECTS) $(LIBS) -o qdda 

qdda.o: qdda.cpp tools.h qdda.h database.h error.h
	g++ -c $(CXXFLAGS) $(CFLAGS) qdda.cpp

database.o: database.cpp tools.h qdda.h database.h error.h
	g++ -c $(CXXFLAGS) $(CFLAGS) database.cpp

lz4.o: lz4/lz4.c lz4/lz4.h
	g++ -c $(CFLAGS) lz4.c

md5.o: md5/md5.c md5/md5.h
	g++ -c $(CFLAGS) md5.c

sqlite3.o: sqlite/sqlite3.c sqlite/sqlite3.h error.h
	g++ -c $(CFLAGS) sqlite3.c

tools.o: tools.cpp tools.h error.h
	g++ -c $(CXXFLAGS) $(CFLAGS) tools.cpp

threads.o: threads.cpp tools.h database.h threads.h qdda.h error.h
	g++ -c $(CXXFLAGS) $(CFLAGS) threads.cpp

output.o: output.cpp tools.h database.h error.h
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

checkhtml:
ifndef MAN2HTML
	@echo man2html not installed
	@exit 10
endif

# Create the archive for rpmbuild
src:	checkrpm allclean
	tar --owner=nobody --group=nobody -jcf $(SOURCES)/$(NAME)-$(VERSION).tbz2 -C $${PWD%/*} --transform "s|src|qdda|" src

# Create zipfile for download
zip:	all
	zip qdda-$(version).zip qdda

# Build the RPM from archive made in 'src' section
rpm:	checkrpm src
	cd .. ; rpmbuild -ba qdda.spec

allclean:
	rm -rf *.o */*.o qdda

html: qdda
	../scripts/gendoc

install: qdda
	install -m 0755 -d $(DESTDIR)/usr/bin
	install -m 0755 -d $(DESTDIR)/usr/share/man/man1
	install -m 0755 -d $(DESTDIR)/etc/bash_completion.d
	install -m 0755 -d $(DESTDIR)/usr/share/qdda

	install -m 0755 -pt $(DESTDIR)/usr/bin/ qdda
	install -m 0755 -pt $(DESTDIR)/usr/share/$(NAME)/ share/*
	
	$(DESTDIR)/usr/bin/qdda --mandump  > $(DESTDIR)/usr/share/man/man1/qdda.1
	$(DESTDIR)/usr/bin/qdda --bashdump > $(DESTDIR)/etc/bash_completion.d/qdda.bash

uninstall:
	rm -f $(DESTDIR)/usr/bin/qdda $(DESTDIR)/etc/bash_completion.d/qdda.bash
	rm -rf $(DESTDIR)/usr/share/qdda

tag:
	git tag v$(version)

.PHONY: install

