# config vars
CC       = gcc
INCLUDES = -I.
LIBS     =
LIBDIR   = ../lib
BINDIR   = ../bin
VERNUM   = 0.0.1

# Compiler options
ifeq ($(debug),yes)
  OPTFLAGS = -O0 -g -Wall -Wextra -ansi -pedantic
else
  ifeq ($(profile),yes)
    OPTFLAGS = -O2 -pg -Wall -Wextra -ansi -pedantic
  else
    OPTFLAGS = -O2 -Wall -Wextra -ansi -pedantic
  endif
endif

# Macro definitions
DEFS  = -D_GNU_SOURCE
#DEFS += -DKL_WITH_DEBUG
#DEFS += -DKL_WITH_MALLOC
#DEFS += -DKL_DPQ_REV
#DEFS += -DKL_WITH_AGGRESSIVE

# Set up commands and flags
CFLAGS = $(OPTFLAGS) $(DEFS) $(INCLUDES) -L$(LIBDIR)
LD     = $(CC) $(CFLAGS)

# What archiving to use
AR = ar cr

# What to use for indexing the archive
RANLIB = ranlib
#RANLIB = ar -ts
#RANLIB =

%.o : %.c
	@$(LD) -c $*.c

default: $(LIBDIR)/libklmalloc.a $(BINDIR)/klmalloc_test  \
  $(BINDIR)/klmalloc_test_cc

$(BINDIR)/klmalloc_test: klmalloc_test.c $(LIBDIR)/libklmalloc.a
	@$(LD) -I../src -o $@ $< $(LIBS) -lklmalloc

$(BINDIR)/klmalloc_test_cc: klmalloc_test.cc
	@g++ $(CFLAGS) -I../src -o $@ $^

$(LIBDIR)/libklmalloc.a: klmalloc.o kldpq.o klhmap.o klsplay.o klutil.o
	@$(AR) $(LIBDIR)/libklmalloc$(VERNUM).a $^
	@$(RANLIB) $(LIBDIR)/libklmalloc$(VERNUM).a
	@ln -sf libklmalloc$(VERNUM).a $(LIBDIR)/libklmalloc.a

clean:
	@rm -f *.o

distclean: clean
	@rm -f $(LIBDIR)/libklmalloc$(VERNUM).a $(LIBDIR)/libklmalloc.a
	@rm -f $(BINDIR)/kldpq $(BINDIR)/klhmap $(BINDIR)/klsplay $(BINDIR)/klmalloc
	@rm -f $(BINDIR)/klmalloc_test $(BINDIR)/klmalloc_test_cc
