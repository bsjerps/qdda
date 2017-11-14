#============================================================================
# Title       : Makefile
# Description : Makefile for qdda (quick & dirty dedupe analyzer)
# Author      : Bart Sjerps <bart@outrun.nl>
# License     : GPLv3+
# URL         : http://outrun.nl/wiki/qdda
# ---------------------------------------------------------------------------

prefix     = /usr/local
sysconfdir = /etc

# CFLAGS  += -Wno-unused-but-set-variable
CFLAGS    += -Wall
CFLAGS    += -Wno-missing-braces 
CXXFLAGS  += -std=c++0x
CFLAGS    += -O3
LIBS       = -lsqlite3 -lstdc++
STATIC     = /usr/lib64/liblz4.a

all: qdda

# Static linking of lz4 avoids having to install dependencies
# The other libs are usually available on most Linux systems
# Note that building on newer systems causes problems with
# older versions of LIBC and STDC++ libraries.
# LZ4 needs to be version 1.31 (or higher).

qdda: md5.o qdda.o database.o tools.o longhelp.o help.o
	g++ $(LDFLAGS) qdda.o md5.o database.o tools.o longhelp.o help.o $(LIBS) $(STATIC) -o qdda 

qdda.o: qdda.cpp tools.h qdda.h
	g++ -c $(CXXFLAGS) $(CFLAGS) qdda.cpp

database.o: database.cpp tools.h qdda.h
	g++ -c $(CXXFLAGS) $(CFLAGS) database.cpp

md5.o: md5.c md5.h
	gcc -c $(CFLAGS) md5.c

tools.o: tools.cpp tools.h
	g++ -c $(CXXFLAGS) $(CFLAGS) tools.cpp

help.o: help.txt
	objcopy -I binary -O elf64-x86-64 -B i386 help.txt help.o

longhelp.o: longhelp.txt
	objcopy -I binary -O elf64-x86-64 -B i386 longhelp.txt longhelp.o

clean:
	rm -rf *.o qdda


