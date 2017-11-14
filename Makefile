#============================================================================
# Title       : Makefile
# Description : Makefile for qdda (quick & dirty dedupe analyzer)
# Author      : Bart Sjerps <bart@outrun.nl>
# License     : GPLv3+
# URL         : http://outrun.nl/wiki/qdda
# ---------------------------------------------------------------------------

prefix     = /usr/local
sysconfdir = /etc

all: qdda

qdda:
	cd src && $(MAKE) -w all

clean:
	cd src && $(MAKE) clean

install: qdda
	install -d 0755 $(prefix)/bin
	install -d 0755 $(prefix)/libexec
	install -d 0755 $(sysconfdir)/bash_completion.d
	install -m 0755 src/qdda $(prefix)/bin
	install -m 0755 bin/acl-qdda $(prefix)/bin
	install -m 0755 bin/qdda-acl $(prefix)/libexec
	install -m 0644 bin/qdda.bash $(sysconfdir)/bash_completion.d
	install -m 0644 COPYING $(datadir)/qdda
	install -m 0644 README.md $(datadir)/qdda
	install -m 0644 share/* $(datadir)/qdda

.PHONY: install

