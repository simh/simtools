# *** NOTE ***
# This makefile is set up for Linux.  It will need some (small) changes to
# build under something else.  See notes in comments below, marked with ***

VERSION = 2.5

# objects
OBJS=\
	rstsflx.o \
	fip.o \
	rtime.o \
	filename.o \
	doget.o \
	dolist.o \
	doalloc.o \
	docomp.o \
	dotype.o \
	doput.o \
	dodump.o \
	dodelete.o \
	dorename.o \
	dorts.o \
	doprot.o \
	dodir.o \
	doident.o \
	doinit.o \
	dohook.o \
	scancmd.o \
	doclean.o \
	fileio.o \
	diskio.o \
	absio.o

# Flags and the like

# *** change the two lines below as needed for your C compiler.
CC ?= gcc
OPTIMIZE ?= -O2 -g2
LDFLAGS ?= -g2

DEFINES = 
CFLAGS = $(OPTIMIZE) $(DEFINES) $(EXTRAFLAGS)

KITNAME = flx-$(VERSION)
DIR = flx

# Rules

S = $(OBJS:.o=.c) 
SRCS = $(S:absio.c=unxabsio.c)

# ***  comment out or delete this first rule if not building on DOS
#flx.exe: flx
#	strip flx
#	coff2exe -s /djgpp/bin/go32.exe flx

flx: $(OBJS)
	$(CC) $(LDFLAGS) -o flx $(OBJS) $(EXTRAOBJS) -lreadline -lncurses $(EXTRAFLAGS)

# *** the rule below builds absio.o.  You need to use as source file
# *** an appropriate file; in Unix that's probably unxabsio.c but check
# *** the source file to be sure.

absio.o: unxabsio.c
	$(CC) -c -o absio.o $(CFLAGS) $<

# general build rule for all other object files:
.c.o:
	$(CC) -c $(CFLAGS) $<

kit:
	rm -f *~
	cd ..; tar cvzf $(KITNAME).tar.gz \
	$(DIR)/README $(DIR)/COPYING $(DIR)/BUGS $(DIR)/HISTORY \
	$(DIR)/Makefile* $(DIR)/*.c $(DIR)/*.h \
	$(DIR)/*.doc $(DIR)/*.pdf $(DIR)/fdprm

clean:
	rm -f *.o flx flx.exe flx.dep


flx.dep:
	gcc -MM $(SRCS) > flx.dep

# the one below is created by make depend
include flx.dep
