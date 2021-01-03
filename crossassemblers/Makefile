# all of these can be over-ridden on the "make" command line if they don't suit your environment.

CFLAGS=-O2 -Wall -Wshadow -Wextra -pedantic -Woverflow -Wstrict-overflow
BIN=/usr/local/bin
INSTALL=install
CC=gcc

SUBDIRS=hpasm macro1 macro11 macro7 macro8x

.PHONY: all clean install uninstall

# Omitted: macro11: needs more complicated Makefiles.
all:
	for subdir in $(SUBDIRS); do \
		$(MAKE) -C $$subdir CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)"; \
	done

clean install uninstall:
	for subdir in $(SUBDIRS); do \
		$(MAKE) -C $$subdir CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" $@; \
	done
