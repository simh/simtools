# all of these can be over-ridden on the "make" command line if they don't suit your environment.

CFLAGS=-O2 -Wall -Wshadow -Wextra -pedantic -Woverflow -Wstrict-overflow
BIN=/usr/local/bin
INSTALL=install
CC=gcc

.PHONY: all clean install uninstall

# Omitted: macro11: needs more complicated Makefiles.
all:
	cd hpasm && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)"
	cd macro1 && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)"
	cd macro7 && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)"
	cd macro8x && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)"

clean:
	cd hpasm && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" clean
	cd macro1 && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" clean
	cd macro7 && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" clean
	cd macro8x && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" clean

install:
	cd hpasm && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" install
	cd macro1 && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" install
	cd macro7 && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" install
	cd macro8x && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" install

uninstall:
	cd hpasm && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" uninstall
	cd macro1 && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" uninstall
	cd macro7 && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" uninstall
	cd macro8x && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" uninstall
