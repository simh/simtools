# all of these can be over-ridden on the "make" command line if don't suit
# your environment
TOOL=dbtap
CFLAGS=-O2 -Wall -Wshadow -Wextra -pedantic -Woverflow -Wstrict-overflow
BIN=/usr/local/bin
INSTALL=install
CC=gcc

$(TOOL): $(TOOL).c tapeio.c dos11.c tapeio.h dos11.h tap.h defs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $(TOOL) $(TOOL).c tapeio.c dos11.c $(LDLIBS)

.PHONY: clean install uninstall

clean:
	rm -f $(TOOL)

install: $(TOOL)
	$(INSTALL) -p -m u=rx,g=rx,o=rx $(TOOL) $(BIN)

uninstall:
	rm -f $(BIN)/$(TOOL)
