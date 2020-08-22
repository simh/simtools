#####
#
# Makefile for macro11 and dumpobj
#

WARNS ?= -Wall -Wshadow -Wextra -pedantic -Woverflow -Wstrict-overflow
OBJFORMAT = -DDEFAULT_OBJECTFORMAT_RT11=0
CFLAGS ?= -O2 -ggdb -std=gnu99 $(WARNS) $(OBJFORMAT)

MACRO11_SRCS = macro11.c \
	assemble.c assemble_globals.c assemble_aux.c	\
	extree.c listing.c macros.c parse.c rept_irpc.c symbols.c \
	mlb2.c mlb-rsx.c mlb-rt11.c object.c stream2.c util.c rad50.c

MACRO11_OBJS = $(MACRO11_SRCS:.c=.o)

DUMPOBJ_SRCS = dumpobj.c rad50.c

DUMPOBJ_OBJS = $(DUMPOBJ_SRCS:.c=.o)

ALL_SRCS = $(MACRO11_SRCS) $(DUMPOBJ_SRCS)

all: macro11 dumpobj

tags: macro11 dumpobj
	ctags *.c *.h

macro11: git-info.h $(MACRO11_OBJS) Makefile
	$(CC) $(CFLAGS) -o macro11 $(MACRO11_OBJS) -lm

dumpobj: $(DUMPOBJ_OBJS) Makefile
	$(CC) $(CFLAGS) -o dumpobj $(DUMPOBJ_OBJS)

$(MACRO11_OBJS): Makefile
$(DUMPOBJ_OBJS): Makefile

git-info.h:
	./make-git-info

# Bootstrap dependency on the git header file, which otherwise
# gets generated too late.
macro11.o: git-info.h
macro11.c: git-info.h

clean:
	-rm -f $(MACRO11_OBJS) $(DUMPOBJ_OBJS) macro11 dumpobj
	-rm -f *.d
	-rm -f git-info.h

# Since the only tests we have so far are for crashes,
# just try to assemble. Later, we will need expected/actual tests.

# Test that all options requiring a value bail out if it's not present.
argtests: macro11
	@ for OPT in -e -d -m -p -o -l -ysl ; do \
	  ./macro11 foo.mac $$OPT     2> /dev/null; \
	  if (( $$? == 1 )); then echo PASS; else echo FAIL; fi; \
	  echo "  $$OPT missing value"; \
	  ./macro11 foo.mac $$OPT -v  2> /dev/null; \
	  if (( $$? == 1 )); then echo PASS; else echo FAIL; fi; \
	  echo "  $$OPT fol. by option"; \
	  done
	@ ./macro11 foo.mac $$OPT -x -v 2> /dev/null; \
	  if (( $$? == 1 )); then echo PASS; else echo FAIL; fi; \
	   echo "  -x must be the last option"

tests: macro11 argtests
	@ ACTUAL=`./macro11 tests/test-undef.mac 2>&1`; \
	if [ "tests/test-undef.mac:1: ***ERROR MACRO .TTYOU not found" == "$$ACTUAL" ]; then echo PASS; else echo FAIL; fi; \
	echo "  test-undef.mac"

# Automatic dependency generation

ifneq ($(MAKECMDGOALS),clean)
-include $(ALL_SRCS:.c=.d)
endif

# Make .d files as side effect of compiling .c to .o
%.d %.o: %.c
	$(CC) $(CFLAGS) -c -o $*.o $<
	@set -e; rm -f $*.d; \
	    $(CC) -MM $(CPPFLAGS) $< > $@.$$$$; \
	    sed 's,\($*\)\.o[ :]*,\1.o \1.d : ,g' < $@.$$$$ > $*.d; \
	    rm -f $@.$$$$
