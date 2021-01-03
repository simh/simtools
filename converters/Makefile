# all of these can be over-ridden on the "make" command line if they don't suit your environment.

CFLAGS=-O2 -Wall -Wshadow -Wextra -pedantic -Woverflow -Wstrict-overflow
BIN=/usr/local/bin
INSTALL=install
CC=gcc

SUBDIRS=asc cosy decsys dtos8cvt fsio gt7cvt hpconvert indent littcvt m8376 \
	mksimtape mt2tpc mtcvt23 mtcvtfix mtcvtodd noff sfmtcvt strrem strsub \
	tar2mt tp512cvt tpc2mt

.PHONY: all clean install uninstall

all:
	for subdir in $(SUBDIRS); do \
		$(MAKE) -C $$subdir CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)"; \
	done

clean install uninstall:
	for subdir in $(SUBDIRS); do \
		$(MAKE) -C $$subdir CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" $@; \
	done
