# config vars
CC       = gcc
CPP      = g++
INCLUDES = -I.
LIBDIR   = ../lib
BINDIR   = ../bin
VERNUM   = 0.0.2

# Compiler options
ifeq ($(debug),yes)
  OPTFLAGS = -O0 -g -Wall -Wextra -ansi -pedantic
else
  ifeq ($(profile),yes)
    OPTFLAGS = -O2 -pg -fprofile-arcs -ftest-coverage -Wall -Wextra -ansi \
               -pedantic
  else
    OPTFLAGS = -O2 -Wall -Wextra -ansi -pedantic
  endif
endif

# Macro definitions
DEFS  = -D_GNU_SOURCE
#DEFS += -DKL_MALLOC           # overload malloc funcs to use klmalloc funcs
#DEFS += -DKL_WITH_HMAP        # use hash map instead of splay tree
#DEFS += -DKL_WITH_STDMALLOC   # use malloc in klmalloc funcs instead of mmap
#DEFS += -DKL_DPQ_REV          # free entry from back of dpq instead of front
#DEFS += -DKL_WITH_AGGRESSIVE  # release memory immediately after use

# Set up commands and flags
CFLAGS = $(OPTFLAGS) $(DEFS) $(INCLUDES)
LD     = $(CC) $(CFLAGS)

# What archiving to use
AR = ar cr

# What to use for indexing the archive
RANLIB = ranlib
#RANLIB = ar -ts
#RANLIB =

%.o : %.c
	$(LD) -c $*.c

default: $(LIBDIR)/libklmalloc.a $(BINDIR)/klmalloc_test  \
  $(BINDIR)/klmalloc_test_cc

$(BINDIR)/klmalloc_test: klmalloc_test.c $(LIBDIR)/libklmalloc.a
	$(CC) $(OPTFLAGS) -I../src -L$(LIBDIR) -o $@ $< -lklmalloc

$(BINDIR)/klmalloc_test_cc: klmalloc_test.cc
	$(CPP) $(OPTFLAGS) -I../src -o $@ $^

$(LIBDIR)/libklmalloc.a: klmalloc.o kldpq.o klhmap.o klsplay.o klutil.o
	$(AR) $(LIBDIR)/libklmalloc$(VERNUM).a $^
	$(RANLIB) $(LIBDIR)/libklmalloc$(VERNUM).a
	ln -sf libklmalloc$(VERNUM).a $(LIBDIR)/libklmalloc.a

clean:
	rm -vf *.o

distclean: clean
	rm -vf $(LIBDIR)/libklmalloc$(VERNUM).a $(LIBDIR)/libklmalloc.a
	rm -vf $(BINDIR)/kldpq $(BINDIR)/klhmap $(BINDIR)/klsplay $(BINDIR)/klmalloc
	rm -vf $(BINDIR)/klmalloc_test $(BINDIR)/klmalloc_test_cc
