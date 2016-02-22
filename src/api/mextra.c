/*
Copyright (c) 2015,2016 Jeremy Iverson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/


#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif


#include <malloc.h> /* struct mallinfo */
#include <string.h> /* memset, strncmp */
#include "common.h"
#include "ipc.h"
#include "lock.h"
#include "sbma.h"
#include "vmm.h"


/****************************************************************************/
/*! Compate two strings and check if the option has been seen before. */
/****************************************************************************/
#define SBMA_OPTCMP(__OPT, __SEEN, __TOK, __STR, __NUM)\
  ((0 == strncmp(__TOK, __STR, __NUM)) &&\
   ('\0' == __TOK[__NUM]) &&\
   ((__OPT) == (((__SEEN)^=(__OPT))&(__OPT))))


/****************************************************************************/
/*! Set parameters for the sbmalloc subsystem. */
/****************************************************************************/
SBMA_EXTERN int
sbma_mallopt(int const __param, int const __value)
{
  int ret;

  ret = lock_get(&(_vmm_.lock));
  if (-1 == ret)
    return -1;

  switch (__param) {
    case M_VMMOPTS:
    if (VMM_INVLD == (__value&VMM_INVLD))
      goto CLEANUP;
    _vmm_.opts = __value;
    break;

    default:
    goto CLEANUP;
  }

  ret = lock_let(&(_vmm_.lock));
  if (-1 == ret)
    goto CLEANUP;

  return 0;

  CLEANUP:
  ret = lock_let(&(_vmm_.lock));
  ASSERT(-1 != ret);
  return -1;
}


/****************************************************************************/
/*! Parse a string into options. */
/****************************************************************************/
SBMA_EXTERN int
sbma_parse_optstr(char const * const __opt_str)
{
  int opts=0, seen=0;
  int all=(VMM_RSDNT|VMM_LZYRD|VMM_AGGCH|VMM_GHOST|VMM_MERGE|VMM_METACH|\
    VMM_MLOCK|VMM_CHECK|VMM_EXTRA|VMM_OSVMM);
  char * tok;
  char str[512];

  if (strlen(__opt_str) > sizeof(str)-1)
    goto CLEANUP;

  strncpy(str, __opt_str, sizeof(str));

  tok = strtok(str, ",");
  while (NULL != tok) {
    if (SBMA_OPTCMP(VMM_RSDNT, seen, tok, "evict", 5)) {
    }
    else if (SBMA_OPTCMP(VMM_RSDNT, seen, tok, "rsdnt", 5)) {
      opts |= VMM_RSDNT;
    }
    else if (SBMA_OPTCMP(VMM_LZYRD, seen, tok, "aggrd", 5)) {
    }
    else if (SBMA_OPTCMP(VMM_LZYRD, seen, tok, "lzyrd", 5)) {
      opts |= VMM_LZYRD;
    }
    else if (SBMA_OPTCMP(VMM_ADMITD, seen, tok, "admitr", 6)) {
    }
    else if (SBMA_OPTCMP(VMM_ADMITD, seen, tok, "admitd", 6)) {
      opts |= VMM_ADMITD;
    }
    else if (SBMA_OPTCMP(VMM_AGGCH, seen, tok, "noaggch", 7)) {
    }
    else if (SBMA_OPTCMP(VMM_AGGCH, seen, tok, "aggch", 5)) {
      opts |= VMM_AGGCH;
    }
    else if (SBMA_OPTCMP(VMM_GHOST, seen, tok, "noghost", 7)) {
    }
    else if (SBMA_OPTCMP(VMM_GHOST, seen, tok, "ghost", 5)) {
      opts |= VMM_GHOST;
    }
    else if (SBMA_OPTCMP(VMM_MERGE, seen, tok, "nomerge", 7)) {
    }
    else if (SBMA_OPTCMP(VMM_MERGE, seen, tok, "merge", 5)) {
      opts |= VMM_MERGE;
    }
    else if (SBMA_OPTCMP(VMM_METACH, seen, tok, "nometach", 8)) {
    }
    else if (SBMA_OPTCMP(VMM_METACH, seen, tok, "metach", 6)) {
      opts |= VMM_METACH;
    }
    else if (SBMA_OPTCMP(VMM_MLOCK, seen, tok, "nomlock", 7)) {
    }
    else if (SBMA_OPTCMP(VMM_MLOCK, seen, tok, "mlock", 5)) {
      opts |= VMM_MLOCK;
    }
    else if (SBMA_OPTCMP((VMM_CHECK|VMM_EXTRA), seen, tok, "nocheck", 7)) {
    }
    else if (SBMA_OPTCMP((VMM_CHECK|VMM_EXTRA), seen, tok, "check", 5)) {
      opts |= VMM_CHECK;
    }
    else if (SBMA_OPTCMP((VMM_CHECK|VMM_EXTRA), seen, tok, "extra", 5)) {
      opts |= (VMM_CHECK|VMM_EXTRA);
    }
    else if (SBMA_OPTCMP(VMM_OSVMM, seen, tok, "noosvmm", 7)) {
    }
    else if (SBMA_OPTCMP(VMM_OSVMM, seen, tok, "osvmm", 5)) {
      opts |= VMM_OSVMM;
    }
    else if (SBMA_OPTCMP(all, seen, tok, "default", 7)) {
      opts |= (VMM_LZYRD|VMM_MERGE);
    }
    else {
      goto CLEANUP;
    }

    tok = strtok(NULL, ",");
  }

  /* VMM_OSVMM is not valid with any other options */
  if (VMM_OSVMM == (opts&VMM_OSVMM) && VMM_OSVMM != opts)
    goto CLEANUP;

  /* VMM_AGGCH is only valid without VMM_LZYRD */
  if (VMM_AGGCH == (opts&(VMM_LZYRD|VMM_AGGCH)))
    goto CLEANUP;

  /* VMM_EXTRA is only valid with VMM_CHECK */
  if (VMM_EXTRA == (opts&(VMM_CHECK|VMM_EXTRA)))
    goto CLEANUP;

  goto RETURN;

  CLEANUP:
  opts = VMM_INVLD;

  RETURN:
  return opts;
}


/****************************************************************************/
/*! Return some memory statistics */
/****************************************************************************/
SBMA_EXTERN struct mallinfo
sbma_mallinfo(void)
{
  struct mallinfo mi;

  memset(&mi, 0, sizeof(struct mallinfo));

  mi.smblks   = _vmm_.numipc;  /* received SIGIPC faults */
  mi.ordblks  = _vmm_.numhipc; /* honored SIGIPC faults */

  mi.usmblks  = _vmm_.numrd; /* syspages read from disk */
  mi.fsmblks  = _vmm_.numwr; /* syspages wrote to disk */
  mi.uordblks = _vmm_.numrf; /* read faults */
  mi.fordblks = _vmm_.numwf; /* write faults */

  if (0 == _vmm_.ipc.init) {
    mi.hblks = _vmm_.ipc.curpages;  /* syspages loaded */
  }
  else {
    mi.hblks = _vmm_.ipc.c_mem[_vmm_.ipc.id]; /* ... */
  }
  mi.hblkhd   = _vmm_.ipc.maxpages; /* high water mark for loaded syspages */
  mi.keepcost = _vmm_.numpages;     /* syspages allocated */

  return mi;
}


/****************************************************************************/
/*! Return some timing statistics */
/****************************************************************************/
SBMA_EXTERN struct sbma_timeinfo
sbma_timeinfo(void)
{
  struct sbma_timeinfo ti;

  memset(&ti, 0, sizeof(struct sbma_timeinfo));

  ti.tv_rd = _vmm_.tmrrd;
  ti.tv_wr = _vmm_.tmrwr;
  //ti.tv_ad = _vmm_.tmrad;
  //ti.tv_ev = _vmm_.tmrev;

  return ti;
}


/****************************************************************************/
/*! Inform runtime to allow signals to be received. */
/****************************************************************************/
SBMA_EXTERN int
sbma_sigon(void)
{
  ipc_sigon(&(_vmm_.ipc));
  return 0;
}


/****************************************************************************/
/*! Inform runtime to disallow signals from being received. */
/****************************************************************************/
SBMA_EXTERN int
sbma_sigoff(void)
{
  ipc_sigoff(&(_vmm_.ipc));
  return 0;
}


#ifdef TEST
int
main(int argc, char * argv[])
{
  if (0 == argc || NULL == argv) {}

  return 0;
}
#endif
