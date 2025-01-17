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

# Sub-directories holding the actual sources
PROJDIRS := src/api src/include src/ipc src/klmalloc src/lock src/mmu src/vmm

# List of all non-source files/directories that are part of the distribution
AUXFILES := AUTHORS ChangeLog COPYING Makefile NEWS README.md doc refs bench


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
ARFLAGS  := crsP
WARNING  := -Wall -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align \
            -Wwrite-strings -Wmissing-prototypes -Wmissing-declarations \
            -Wredundant-decls -Wnested-externs -Winline -Wno-long-long \
            -Wuninitialized -Wstrict-prototypes
OPTIMIZE := -O0 -g
CFLAGS   := -std=c99 $(OPTIMIZE) $(WARNING)
LDFLAGS  := -pthread
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
