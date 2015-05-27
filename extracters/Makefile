# all of these can be over-ridden on the "make" command line if they don't suit your environment.

CFLAGS=-O2 -Wall -Wshadow -Wextra -pedantic -Woverflow -Wstrict-overflow
BIN=/usr/local/bin
INSTALL=install
#CC=gcc

.PHONY: all clean install uninstall

# Omitted: backup, ods2: need more complicated Makefiles.
all:
	cd ckabstape && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)"
	cd mmdir && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)"
	cd mtdump && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)"
	cd rawcopy && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)"
	cd sdsdump && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)"
	cd tpdump && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)"

clean:
	cd ckabstape && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" clean
	cd mmdir && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" clean
	cd mtdump && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" clean
	cd rawcopy && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" clean
	cd sdsdump && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" clean
	cd tpdump && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" clean

install:
	cd ckabstape && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" install
	cd mmdir && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" install
	cd mtdump && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" install
	cd rawcopy && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" install
	cd sdsdump && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" install
	cd tpdump && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" install

uninstall:
	cd ckabstape && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" uninstall
	cd mmdir && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" uninstall
	cd mtdump && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" uninstall
	cd rawcopy && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" uninstall
	cd sdsdump && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" uninstall
	cd tpdump && $(MAKE) CFLAGS="$(CFLAGS)" BIN="$(BIN)" INSTALL="$(INSTALL)" CC="$(CC)" uninstall
