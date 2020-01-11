CFLAGS=-O2 -Wall -Wshadow -Wextra -pedantic -Woverflow -Wstrict-overflow
#CFLAGS=-g -Wall -Wshadow -Wextra -pedantic -Woverflow -Wstrict-overflow
DEFINES=-DDEBUG
BIN=/usr/local/bin
MAN=/usr/local/man/man1
INSTALL=install
CC=gcc

EXECUTABLE=fsio
SOURCES=fsio.c declib.c tape.c dos11.c rt11.c dosmt.c local.c os8.c
INCLUDES=fsio.h declib.h tape.h dos11.h rt11.h dosmt.h os8.h
LIBS=-lreadline
MANPAGE=fsio.1
MANPAGE_DOS=fsio-dos11.1
MANPAGE_RT=fsio-rt11.1
MANPAGE_DOSMT=fsio-dosmt.1
MANPAGE_OS8=fsio-os8.1
ARCHIVE=fsio.tgz

RELEASEFILES=$(BIN)/$(EXECUTABLE)
RELEASEFILES+=$(MAN)/$(MANPAGE)
RELEASEFILES+=$(MAN)/$(MANPAGE_DOS)
RELEASEFILES+=$(MAN)/$(MANPAGE_RT)
RELEASEFILES+=$(MAN)/$(MANPAGE_DOSMT)
RELEASEFILES+=$(MAN)/$(MANPAGE_OS8)
RELEASEFILES+=./fsio.txt ./fsioSimh.txt

$(EXECUTABLE): $(SOURCES) $(INCLUDES) Makefile
	$(CC) $(CFLAGS) $(DEFINES) -o $(EXECUTABLE) $(SOURCES) $(LIBS)

.phony: clean install uninstall

clean:
	rm -f $(EXECUTABLE)

install: $(EXECUTABLE) $(MANPAGE) $(MANPAGE_DOS) $(MANPAGE_RT)
	$(INSTALL) -p -m u=rx,g=rx,o=rx $(EXECUTABLE) $(BIN)
	mkdir -p $(MAN)
	$(INSTALL) -p -m u=r,g=r,o=r $(MANPAGE) $(MAN)
	$(INSTALL) -p -m u=r,g=r,o=r $(MANPAGE_DOS) $(MAN)
	$(INSTALL) -p -m u=r,g=r,o=r $(MANPAGE_RT) $(MAN)
	$(INSTALL) -p -m u=r,g=r,o=r $(MANPAGE_DOSMT) $(MAN)
	$(INSTALL) -p -m u=r,g=r,o=r $(MANPAGE_OS8) $(MAN)

uninstall:
	rm -f $(BIN)/$(EXECUTABLE)
	rm -f $(MAN)/$(MANPAGE)
	rm -f $(MAN)/$(MANPAGE_DOS)
	rm -f $(MAN)/$(MANPAGE_RT)
	rm -f $(MAN)/$(MANPAGE_DOSMT)
	rm -f $(MAN)/$(MANPAGE_OS8)

# This assumes that fsio has been "installed" on the current system
archive:	$(RELEASEFILES)
	tar czvPf $(ARCHIVE) $(RELEASEFILES)
