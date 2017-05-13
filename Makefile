#============================================================================
# Title       : Makefile
# Description : Makefile for qdda (quick & dirty dedupe analyzer)
# Author      : Bart Sjerps <bart@outrun.nl>
# License     : GPLv3+
# URL         : http://outrun.nl/wiki/qdda
# ---------------------------------------------------------------------------

prefix = /usr/local

# CFLAGS  += -Wno-unused-but-set-variable
CFLAGS  += -Wall
CFLAGS  += -std=c++0x
CFLAGS  += -O2
LIBS     = -lsqlite3 -lstdc++ -lcrypto
STATIC   = /usr/lib64/liblz4.a

all: qdda

# Static linking of lz4 avoids having to install dependencies
# The other libs are usually available on most Linux systems
# Note that building on newer systems causes problems with
# older versions of LIBC and STDC++ libraries.
# LZ4 needs to be version 1.31 (or higher).

qdda: qdda.o
	g++ $(LDFLAGS) qdda.o $(LIBS) $(STATIC) -o qdda 

qdda.o: qdda.cpp
	g++ -c $(CFLAGS) qdda.cpp

clean:
	rm -rf *.o qdda

install: qdda
	install -d 0755 $(prefix)/bin
	install -m 0755 qdda $(prefix)/bin

.PHONY: install

