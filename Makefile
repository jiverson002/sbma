#-------------------------------------------------------------------------------
# COPYRIGHT NOTICE
#-------------------------------------------------------------------------------
#{{{1
# Copyright (c) 2015,2016 Jeremy Iverson
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#}}}1


# Name of project
PROJECT := SBMA

# Name of library
LIBRARY := sbma

# Directory which holds $(LIBRARY).h
#   $(LIBRARY).h is the header file which will be installed along with the
#   library. It MUST contain the version information for the project in the
#   following macros: $(PROJECT)_MAJOR, $(PROJECT)_MINOR, $(PROJECT)_PATCH, and
#   optionally $(PROJECT)_RCAND.
INCLUDE := src/include


#-------------------------------------------------------------------------------
# PROGRAMS
#-------------------------------------------------------------------------------
#{{{1
# Program variables
AR := ar
RM := rm -f
ECHO := echo
TOUCH := touch
INSTALL := install -CDv --mode=0644
INSTALLLOG := InstallManifest.txt
CC := cc
LD := cc
#}}}1


#-------------------------------------------------------------------------------
# FLAGS
#-------------------------------------------------------------------------------
#{{{1
# TODO Add -Wconversion
WARNING  := -Wall -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align \
            -Wwrite-strings -Wmissing-prototypes -Wmissing-declarations \
            -Wredundant-decls -Wnested-externs -Winline -Wno-long-long \
            -Wuninitialized -Wstrict-prototypes
OPTIMIZE := -O0 -g
CFLAGS   := $(OPTIMIZE) $(WARNING)
LDFLAGS  := -pthread
ARFLAGS  := crsP
TESTLIBS := -lrt -ldl
#}}}1


#-------------------------------------------------------------------------------
# PATHS
#-------------------------------------------------------------------------------
#{{{1
includedir = $(prefix)/include
libdir = $(exec_prefix)/lib
exec_prefix = $(prefix)
prefix = /usr/local
#}}}1


#-------------------------------------------------------------------------------
# FILES/DIRECTORIES
#-------------------------------------------------------------------------------
#{{{1
# This is a list of all non-source files/directories that are part of the distribution.
AUXFILES := AUTHORS ChangeLog COPYING Makefile NEWS README.md doc refs bench

# Subdirectories holding the actual sources
PROJDIRS := src/api src/include src/ipc src/klmalloc src/lock src/mmu src/vmm
#}}}1


#===============================================================================
# MUST NOT CHANGE ANYTHING BELOW HERE
#===============================================================================


#-------------------------------------------------------------------------------
# INTERNAL VARIABLES
#-------------------------------------------------------------------------------
#{{{1
# Version information
DATE    := $(shell date)
COMMIT  := $(shell git rev-parse --short HEAD)
VERSION := $(shell grep -e '^\#define $(PROJECT)_MAJOR' \
                        -e '^\#define $(PROJECT)_MINOR' \
                        -e '^\#define $(PROJECT)_PATCH' \
                        -e '^\#define $(PROJECT)_RCAND' \
                        $(INCLUDE)/$(LIBRARY).h \
                 | awk '{print $$3}' \
                 | paste -d ' ' - - - - \
                 | awk '{printf "%d.%d.%d%s", $$1,$$2,$$3,$$4}')
#}}}1


#-------------------------------------------------------------------------------
# FILES
#-------------------------------------------------------------------------------
#{{{1
SRCFILES    := $(shell find $(PROJDIRS) -type f -name "*.c")
HDRFILES    := $(shell find $(PROJDIRS) -type f -name "*.h")

OBJFILES    := $(patsubst %.c,%.o,$(SRCFILES))
TSTFILES    := $(patsubst %.c,%_t,$(SRCFILES))

DEPFILES    := $(patsubst %.c,%.d,$(SRCFILES))
TSTDEPFILES := $(patsubst %,%.d,$(TSTFILES))

ALLFILES    := $(SRCFILES) $(HDRFILES) $(AUXFILES)

LIB         := lib$(LIBRARY).a
DIST        := $(LIBRARY)-$(VERSION).tar.gz
#}}}1


#-------------------------------------------------------------------------------
# COMPILE TARGETS
#-------------------------------------------------------------------------------
#{{{1
$(LIB): $(OBJFILES)
	@echo "  AR       $@"
	@$(AR) $(ARFLAGS) $@ $?
	@echo "  RANLIB   $@"

-include $(DEPFILES) $(TSTDEPFILES)

# TODO Remove hard-coded paths (-Isrc/include)
%.o: %.c Makefile
	@echo "  CC       $@"
	@$(CC) $(CFLAGS) -MMD -MP -I$(INCLUDE) \
         -DVERSION="$(VERSION)" -DDATE="$(DATE)" -DCOMMIT="$(COMMIT)" \
         -c $< -o $@

%_t: %.c Makefile $(LIB)
	@echo "  CC       $@"
	@$(CC) $(CFLAGS) -MMD -MP -I$(INCLUDE) $(LDFLAGS) -DTEST \
         -DVERSION="$(VERSION)" -DDATE="$(DATE)" -DCOMMIT="$(COMMIT)" \
         $< $(LIB) -o $@ $(TESTLIBS)

# FIXME This will not update distribution if a file in a directory included in
# ALLFILES is updated/added.
$(DIST): $(ALLFILES)
	-tar czf $(DIST) $(ALLFILES)
#}}}1


#-------------------------------------------------------------------------------
# PHONY TARGETS
#-------------------------------------------------------------------------------
#{{{1
check: $(TSTFILES)
	-@rc=0; count=0; \
    for file in $(TSTFILES); do \
      ./$$file; \
      ret=$$?; \
      rc=`expr $$rc + $$ret`; count=`expr $$count + 1`; \
      if [ $$ret -eq 0 ] ; then \
        echo -n "  PASS"; \
      else \
        echo -n "  FAIL"; \
      fi; \
      echo "     $$file"; ./$$file; \
    done; \
    echo; \
    echo "Tests executed: $$count  Tests failed: $$rc"

clean:
	-$(RM) $(wildcard $(OBJFILES) $(DEPFILES) $(TSTFILES) $(TSTDEPFILES) $(LIB))

dist: $(DIST)

distclean: clean
	-$(RM) $(wildcard $(DIST))

help:
	$(ECHO) "The following are valid targets for this Makefile:"
	$(ECHO) "... $(LIB) (the default if no target is provided)"
	$(ECHO) "... check"
	$(ECHO) "... clean"
	$(ECHO) "... dist"
	$(ECHO) "... distclean"
	$(ECHO) "... help"
	$(ECHO) "... install"
	$(ECHO) "... show"
	$(ECHO) "... todolist"

install: $(LIB)
	@$(ECHO) $(libdir)/$(LIB) > $(INSTALLLOG)
	$(INSTALL) $(LIB) $(libdir)/$(LIB)
	@$(ECHO) $(includedir)/$(LIBRARY).h >> $(INSTALLLOG)
	$(INSTALL) $(INCLUDE)/$(LIBRARY).h $(includedir)/$(LIBRARY).h

show:
	$(ECHO) "$(PROJECT) v$(VERSION) ($(COMMIT)) on $(DATE)"
	$(ECHO) "  includedir=$(includedir)"
	$(ECHO) "  libdir=$(libdir)"
	$(ECHO) "  exec_prefix=$(exec_prefix)"
	$(ECHO) "  prefix=$(prefix)"

todolist:
	-@for file in $(ALLFILES:Makefile=); do fgrep -H -e TODO -e FIXME $$file; done; true
#}}}1


#-------------------------------------------------------------------------------
# SPECIAL TARGETS
#-------------------------------------------------------------------------------
#{{{1
.SILENT: help show
.PHONY: check clean dist distclean help install show todolist
#}}}1


# vim: set foldmethod=marker:
