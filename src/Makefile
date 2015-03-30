.POSIX:
.SILENT: config help maintainer-clean show


#----------------------------------------------------------------------------#
# GLOBAL MACROS                                                              #
#----------------------------------------------------------------------------#
#{{{1
# Configuration variables
AR         = ar
ARFLAGS    = -cr
RANLIB     = ranlib
RM         = rm -f
ECHO       = echo
TOUCH      = touch
SED        = sed
SED_I      = $(SED) -i
INSTALL    = install -CDv --mode=0644
INSTALLLOG = InstallManifest.txt

# Version information
DATE    = `date`
COMMIT  = `git rev-parse --short HEAD`
VERSION = 0.0.0-rc1

# Configuration variables (will be updated automatically by `make config')
# These can be safely configured manually if one so chooses.
config_CC=c99
config_prefix=/usr/local
config_exec_prefix=/usr/local
config_includedir=/usr/local/include
config_libdir=/usr/local/lib
config_build=debug
config_profile=no
config_pthread=no
config_openmp=no
config_mmap=no
config_memalign=yes
config_sbmalloc=no

# Compiler flags
CFLAGS_debug       = -O0 -g -Wall -Wextra -pedantic
CFLAGS_release     = -O3 -DNDEBUG -Wall -Wextra -pedantic
CFLAGS_releaseinfo = -O2 -g -Wall -Wextra -pedantic
PROFILE_yes        = -DUSE_PROFILE -pg
PTHREAD_yes        = -DUSE_PTHREAD -pthread
OPENMP_yes         = -DUSE_OPENMP -fopenmp
MMAP_yes           = -DUSE_MMAP
MEMALIGN_yes       = -DUSE_MEMALIGN
SBMALLOC_yes       = -DUSE_SBMALLOC
#}}}1


##############################################################################
#  Do not change anything below here  ########################################
##############################################################################


#----------------------------------------------------------------------------#
# INTERNAL MACROS                                                            #
#----------------------------------------------------------------------------#
#{{{1
# Default config target parameters
CC            = c99
prefix        = /usr/local
exec_prefix   = $(prefix)
includedir    = $(prefix)/include
libdir        = $(exec_prefix)/lib
build         = debug
with-profile  = no
with-pthread  = no
with-openmp   = no
with-mmap     = no
with-memalign = no
with-sbmalloc = no

# Library information
LIB = libklmalloc.a

# Set up commands and flags
CFLAGS  = $(CFLAGS_$(config_build)) $(PROFILE_$(config_profile))        \
          -DVERSION="$(VERSION)" -DDATE="$(DATE)" -DCOMMIT="$(COMMIT)"  \
          $(PTHREAD_$(config_pthread)) $(OPENMP_$(config_openmp))       \
          $(MMAP_$(config_mmap)) $(MEMALIGN_$(config_memalign))         \
          $(SBMALLOC_$(config_sbmalloc))
LDFLAGS = -I. $(PROFILE_$(config_profile)) $(PTHREAD_$(config_pthread)) \
          $(OPENMP_$(config_openmp))
LDLIBS  = ./$(LIB) -ldl

# Up-to-date file -- whenever a prerequisite of this file changes, any target
# of which this file is a prerequisite will be forced to update.  This is done
# by removing said files whenever the rule for this file is executed.
UTD = .UTD
#}}}1


#----------------------------------------------------------------------------#
# COMPILE TARGETS                                                            #
#----------------------------------------------------------------------------#
#{{{1
.c.a:
	$(config_CC) -c $(CFLAGS) $<
	$(AR) $(ARFLAGS) $@ $*.o

#$(LIB): $(UTD) $(LIB)(sbmalloc.o) $(LIB)(klmalloc.o) $(LIB)(hooks.o)
$(LIB): $(UTD) $(LIB)(sbmalloc.o) $(LIB)(klmalloc.o)
	$(RANLIB) $@
	$(RM) sbmalloc.o klmalloc.o
#	$(RM) sbmalloc.o klmalloc.o hooks.o

test/check: $(UTD) $(LIB) test/check.c
	$(config_CC) $(CFLAGS_$(config_build)) $(LDFLAGS) -o $@ $@.c $(LDLIBS)
#	@$@

test/check1: $(UTD) $(LIB) test/check1.c
	$(config_CC) $(CFLAGS_$(config_build)) $(LDFLAGS) -o $@ $@.c $(LDLIBS)
#	@$@

test/check2: $(UTD) $(LIB) test/check2.c
	$(config_CC) $(CFLAGS_$(config_build)) $(LDFLAGS) -o $@ $@.c $(LDLIBS)
#	@$@

test/check3: $(UTD) $(LIB) test/check3.c
	$(config_CC) $(CFLAGS_$(config_build)) $(LDFLAGS) -o $@ $@.c $(LDLIBS)
#	@$@

$(UTD): sbconfig.h sbmalloc.h klmalloc.h Makefile
	@$(RM) $(LIB) test/check test/check1 test/check2 test/check3
	@$(TOUCH) $(UTD)
#}}}1


#----------------------------------------------------------------------------#
# SYSTEM TARGETS                                                             #
#----------------------------------------------------------------------------#
#{{{1
config:
	$(SED_I) -e 's|^config_CC=.*$$|config_CC=$(CC)|' \
           -e 's|^config_prefix=.*$$|config_prefix=$(prefix)|' \
           -e 's|^config_exec_prefix=.*$$|config_exec_prefix=$(exec_prefix)|' \
           -e 's|^config_bindir=.*$$|config_bindir=$(bindir)|' \
           -e 's|^config_build=.*$$|config_build=$(build)|' \
           -e 's|^config_profile=.*$$|config_profile=$(with-profile)|' \
           -e 's|^config_pthread=.*$$|config_pthread=$(with-pthread)|' \
           -e 's|^config_openmp=.*$$|config_openmp=$(with-openmp)|' \
           -e 's|^config_mmap=.*$$|config_mmap=$(with-mmap)|' \
           -e 's|^config_memalign=.*$$|config_memalign=$(with-memalign)|' \
           -e 's|^config_sbmalloc=.*$$|config_sbmalloc=$(with-sbmalloc)|' \
    Makefile
	$(MAKE) -s show

install: $(LIB)
	@$(ECHO) $(config_includedir)/klmalloc.h > $(INSTALLLOG)
	@$(ECHO) $(config_libdir)/$(LIB) >> $(INSTALLLOG)
	$(INSTALL) klmalloc.h $(config_includedir)
	$(INSTALL) $(LIB) $(config_libdir)

show:
	$(ECHO) "KLMalloc v$(VERSION) configured with: make config \
CC=$(CC) prefix=$(config_prefix) exec_prefix=$(config_exec_prefix) \
includedir=$(config_includedir) libdir=$(libdir) build=$(build) \
with-profile=$(config_profile) with-pthread=$(config_pthread) \
with-openmp=$(config_openmp) with-mmap=$(config_mmap) \
with-memalign=$(config_memalign) with-sbmalloc=$(config_sbmalloc)"

clean:
	$(RM) $(LIB) test/check test/check1 test/check2 test/check3 $(UTD)

help:
	$(ECHO) "The following are valid targets for this Makefile:"
	$(ECHO) "... $(LIB) (the default if no target is provided)"
	$(ECHO) "... test/check"
	$(ECHO) "... test/check1"
	$(ECHO) "... test/check2"
	$(ECHO) "... test/check3"
	$(ECHO) "... config"
	$(ECHO) "... dist (non-portable)"
	$(ECHO) "... install"
	$(ECHO) "... show"
	$(ECHO) "... clean"
	$(ECHO) "... maintainer-clean (non-portable)"
	$(ECHO) "... help"
	$(ECHO) ""
	$(ECHO) "The following are valid variables for the \`config' target:"
	$(ECHO) "... CC=COMPILER (gcc)"
	$(ECHO) ""
	$(ECHO) "The following are valid parameters for the \`config' target:"
	$(ECHO) "... prefix=DIR (/usr/local)"
	$(ECHO) "... exec_prefix=DIR (prefix)"
	$(ECHO) "... bindir=DIR (prefix/bin)"
	$(ECHO) ""
	$(ECHO) "The following are valid options for the \`config' target:"
	$(ECHO) "... build=[debug|release|releaseinfo]"
	$(ECHO) "... with-profile=[no|yes]"
	$(ECHO) "... with-pthread=[no|yes]"
	$(ECHO) "... with-openmp=[no|yes]"
	$(ECHO) "... with-mmap=[no|yes]"
	$(ECHO) "... with-memalign=[no|yes]"
	$(ECHO) "... with-sbmalloc=[no|yes]"
#}}}1


#----------------------------------------------------------------------------#
# MAINTAINER TARGETS (NON-PORTABLE)                                          #
#----------------------------------------------------------------------------#
#{{{1
dist:
	cd .. &&\
  mkdir klmalloc-$(VERSION) &&\
  ln -s ../src klmalloc-$(VERSION)/src &&\
  cp src/Makefile . &&\
  make -sC klmalloc-$(VERSION)/src maintainer-clean > /dev/null &&\
  sed -i 's/^COMMIT \+=.*$$/COMMIT='"$(COMMIT)"'/' src/Makefile &&\
  tar --exclude=.* -czhf klmalloc-$(VERSION).tar.gz klmalloc-$(VERSION) &&\
  mv -f Makefile src &&\
  mv -f klmalloc-$(VERSION).tar.gz src &&\
  rm -rf klmalloc-$(VERSION)

maintainer-clean:
	rm -f klmalloc-$(VERSION).tar.gz
	make clean  > /dev/null
	make config > /dev/null
#}}}1
