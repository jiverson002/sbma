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
config_pthread=no
config_profile=no

# Compiler flags
CFLAGS_debug       = -O0 -g -Wall -Wextra -pedantic
CFLAGS_release     = -O3 -DNDEBUG -Wall -Wextra -pedantic
CFLAGS_releaseinfo = -O2 -g -Wall -Wextra -pedantic
PTHREAD_yes        = -DUSE_PTHREAD -pthread
PROFILE_yes        = -pg
#}}}1


##############################################################################
#  Do not change anything below here  ########################################
##############################################################################


#----------------------------------------------------------------------------#
# INTERNAL MACROS                                                            #
#----------------------------------------------------------------------------#
#{{{1
# Default config target parameters
CC           = c99
prefix       = /usr/local
exec_prefix  = $(prefix)
includedir   = $(prefix)/include
libdir       = $(exec_prefix)/lib
build        = debug
with-pthread = no
with-profile = no

# Library information
LIB = libklmalloc.a

# Set up commands and flags
CFLAGS  = $(CFLAGS_$(config_build)) $(PROFILE_$(config_profile))        \
          -DVERSION="$(VERSION)" -DDATE="$(DATE)" -DCOMMIT="$(COMMIT)"  \
          $(PTHREAD_LIB_$(config_pthread))
LDFLAGS = -I.
LDLIBS  = ./$(LIB)

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

$(LIB): $(UTD) $(LIB)(klmalloc.o)
	$(RANLIB) $@
	$(RM) klmalloc.o

test/test: $(UTD) $(LIB) test/test.c
	$(config_CC) $(CFLAGS_$(config_build)) $(LDFLAGS) -o $@ $@.c $(LDLIBS)
#	@$@

test/check: $(UTD) $(LIB) test/check.c
	$(config_CC) $(CFLAGS_$(config_build)) $(LDFLAGS) -o $@ $@.c $(LDLIBS)
	@$@

$(UTD): klmalloc.h Makefile
	@$(RM) $(LIB) test/check
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
           -e 's|^config_pthread=.*$$|config_pthread=$(with-pthread)|' \
           -e 's|^config_profile=.*$$|config_profile=$(with-profile)|' \
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
with-pthread=$(config_pthread) with-profile=$(config_profile)"

clean:
	$(RM) $(LIB) test/check test/mm $(UTD)

help:
	$(ECHO) "The following are valid targets for this Makefile:"
	$(ECHO) "... $(LIB) (the default if no target is provided)"
	$(ECHO) "... test/check"
	$(ECHO) "... test/mm"
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
	$(ECHO) "... with-pthread=[no|yes]"
	$(ECHO) "... with-profile=[no|yes]"
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
