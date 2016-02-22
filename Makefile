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
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#}}}1
.SILENT: help show
.PHONY: clean help install show


LIB = libsbma.a


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
WARNING := -Wall -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align \
           -Wwrite-strings -Wmissing-prototypes -Wmissing-declarations \
           -Wredundant-decls -Wnested-externs -Winline -Wno-long-long \
           -Wuninitialized -Wstrict-prototypes
CFLAGS  := -std=c99 -O0 -g $(WARNING)
LDFLAGS :=
ARFLAGS := crs
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


#===============================================================================
# DO NOT CHANGE ANYTHING BELOW HERE
#===============================================================================


#-------------------------------------------------------------------------------
# FILES
#-------------------------------------------------------------------------------
#{{{1
# This is a list of all non-source files that are part of the distribution.
AUXFILES := AUTHORS ChangeLog COPYING Makefile NEWS README.md

# Subdirectories holding the actual sources
PROJDIRS := src/api src/include src/ipc src/klmalloc src/lock src/mmu src/vmm

SRCFILES := $(shell find $(PROJDIRS) -type f -name "*.c")
HDRFILES := $(shell find $(PROJDIRS) -type f -name "*.h")

OBJFILES := $(patsubst %.c,%.o,$(SRCFILES))

DEPFILES := $(patsubst %.c,%.d,$(SRCFILES))

ALLFILES := $(SRCFILES) $(HDRFILES) $(AUXFILES)
#}}}1


#-------------------------------------------------------------------------------
# INTERNAL VARIABLES
#-------------------------------------------------------------------------------
#{{{1
# Version information
PROJECT := "SBMA"
DATE    := $(shell date)
COMMIT  := $(shell git rev-parse --short HEAD)
VERSION := $(shell grep -e '^\#define SBMA_MAJOR' -e '^\#define SBMA_MINOR' \
                        -e '^\#define SBMA_PATCH' -e '^\#define SBMA_RCAND' \
                        src/include/sbma.h \
                 | awk '{print $$3}' \
                 | paste -d ' ' - - - - \
                 | awk '{printf "%d.%d.%d%s", $$1,$$2,$$3,$$4}')
#}}}1


#-------------------------------------------------------------------------------
# COMPILE TARGETS
#-------------------------------------------------------------------------------
#{{{1
$(LIB): $(OBJFILES)
	@echo "  AR       $@"
	@echo "  RANLIB   $@"
	@$(AR) $(ARFLAGS) $@ $?

-include $(DEPFILES)

%.o: %.c Makefile
	@echo "  CC       $@"
	@$(CC) $(CFLAGS) -MMD -MP -Isrc/include \
         -DVERSION="$(VERSION)" -DDATE="$(DATE)" -DCOMMIT="$(COMMIT)" \
         -c $< -o $@
#}}}1


#-------------------------------------------------------------------------------
# PHONY TARGETS
#-------------------------------------------------------------------------------
#{{{1
clean:
	$(RM) $(LIB) $(OBJFILES)

help:
	$(ECHO) "The following are valid targets for this Makefile:"
	$(ECHO) "... $(LIB) (the default if no target is provided)"
	$(ECHO) "... install"
	$(ECHO) "... show"
	$(ECHO) "... clean"
	$(ECHO) "... help"

install: $(LIB)
	@$(ECHO) $(libdir)/$(LIB) > $(INSTALLLOG)
	$(INSTALL) $(LIB) $(libdir)

show:
	$(ECHO) "$(PROJECT) v$(VERSION) ($(COMMIT)) on $(DATE)"
	$(ECHO) "  includedir=$(includedir)"
	$(ECHO) "  libdir=$(libdir)"
	$(ECHO) "  exec_prefix=$(exec_prefix)"
	$(ECHO) "  prefix=$(prefix)"
#}}}1


# vim: set foldmethod=marker:
