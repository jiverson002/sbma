#------------------------------------------------------------------------------
# COPYRIGHT NOTICE
#------------------------------------------------------------------------------
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


.POSIX:
.SILENT: help show


#------------------------------------------------------------------------------
# CACHE VARIABLES
#------------------------------------------------------------------------------
#{{{1
# Program variables
RANLIB = ranlib
RM = rm -f
ECHO = echo
TOUCH = touch
INSTALL = install -CDv --mode=0644
INSTALLLOG = InstallManifest.txt

# Config parameters
includedir = $(prefix)/include
libdir = $(exec_prefix)/lib
exec_prefix = $(prefix)
prefix = /usr/local

# Compiler parameters
CC = c99
LD = c99
CFLAGS = -I. $(AUTOFLAGS)
LDFLAGS = -I. $(AUTOFLAGS)
LDLIBS =
#}}}1


#==============================================================================
# DO NOT CHANGE ANYTHING BELOW HERE
#==============================================================================


#------------------------------------------------------------------------------
# INTERNAL VARIABLES
#------------------------------------------------------------------------------
#{{{1
# Version information
PROJECT = "SBMalloc"
DATE = `date`
COMMIT = `git rev-parse --short HEAD`
VERSION = `grep -e '^\#define SBMA_MAJOR' -e '^\#define SBMA_MINOR' -e '^\#define SBMA_PATCH' -e '^\#define SBMA_RCAND' sbma.h | awk '{print $$3}' | paste -d ' ' - - - - | awk '{printf \"%d.%d.%d%s\", $$1,$$2,$$3,$$4}'`

AUTOFLAGS = \
 -O0 -g -Wall -Wextra -pedantic\
 -DVERSION="$(VERSION)" -DDATE="$(DATE)" -DCOMMIT="$(COMMIT)"

# Up-to-date file -- this file is used to ensure that any time the Makefile
# is manually updated, libraries and targets will be rebuilt. This is done
# by removing said files whenever the rule for this file is executed.
UTD = .UTD
#}}}1


#------------------------------------------------------------------------------
# COMPILE TARGETS
#------------------------------------------------------------------------------
#{{{1
libsbma.a: $(UTD) libsbma.a(hooks.o) libsbma.a(ipc.o) libsbma.a(klmalloc.o) libsbma.a(lock.o) libsbma.a(malloc.o) libsbma.a(mcntrl.o) libsbma.a(mextra.o) libsbma.a(mmu.o) libsbma.a(mstate.o) libsbma.a(vmm.o)
	$(RANLIB) $@

$(UTD): Makefile
	@$(RM) libsbma.a
	@$(TOUCH) $(UTD)
#}}}1


#------------------------------------------------------------------------------
# SYSTEM TARGETS
#------------------------------------------------------------------------------
#{{{1
install: libsbma.a
	@$(ECHO) $(libdir)/libsbma.a > $(INSTALLLOG)
	$(INSTALL) libsbma.a $(libdir)

show:
	$(ECHO) "$(PROJECT) v$(VERSION) ($(COMMIT)) on $(DATE)"
	$(ECHO) "  includedir=$(includedir)"
	$(ECHO) "  libdir=$(libdir)"
	$(ECHO) "  exec_prefix=$(exec_prefix)"
	$(ECHO) "  prefix=$(prefix)"

clean:
	$(RM) libsbma.a $(UTD)

help:
	$(ECHO) "The following are valid targets for this Makefile:"
	$(ECHO) "... libsbma.a (the default if no target is provided)"
	$(ECHO) "... install"
	$(ECHO) "... show"
	$(ECHO) "... clean"
	$(ECHO) "... help"
#}}}1
