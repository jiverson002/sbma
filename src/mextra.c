/*
Copyright (c) 2015, Jeremy Iverson
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
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
__sbma_mallopt(int const __param, int const __value)
{
  int ret;

  ret = __lock_get(&(vmm.lock));
  if (-1 == ret)
    return -1;

  switch (__param) {
    case M_VMMOPTS:
    if (VMM_INVLD == (__value&VMM_INVLD))
      goto CLEANUP;
    vmm.opts = __value;
    break;

    default:
    goto CLEANUP;
  }

  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    goto CLEANUP;

  return 0;

  CLEANUP:
  ret = __lock_let(&(vmm.lock));
  ASSERT(-1 != ret);
  return -1;
}
SBMA_EXPORT(default, int
__sbma_mallopt(int const __param, int const __value));


/****************************************************************************/
/*! Parse a string into options. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_parse_optstr(char const * const __opt_str)
{
  int opts=0, seen=0;
  int all=(VMM_RSDNT|VMM_LZYRD|VMM_AGGCH|VMM_GHOST|VMM_MERGE|VMM_METACH|\
    VMM_MLOCK|VMM_CHECK|VMM_EXTRA|VMM_OSVMM);
  char * tok;
  char str[512];

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
SBMA_EXPORT(default, int
__sbma_parse_optstr(char const * const __opt_str));


/****************************************************************************/
/*! Return some memory statistics */
/****************************************************************************/
SBMA_EXTERN struct mallinfo
__sbma_mallinfo(void)
{
  struct mallinfo mi;

  memset(&mi, 0, sizeof(struct mallinfo));

  mi.smblks   = vmm.numipc;  /* received SIGIPC faults */
  mi.ordblks  = vmm.numhipc; /* honored SIGIPC faults */

  mi.usmblks  = vmm.numrd; /* syspages read from disk */
  mi.fsmblks  = vmm.numwr; /* syspages wrote to disk */
  mi.uordblks = vmm.numrf; /* read faults */
  mi.fordblks = vmm.numwf; /* write faults */

  if (0 == vmm.ipc.init)
    mi.hblks = vmm.ipc.curpages;  /* syspages loaded */
  else
    mi.hblks = vmm.ipc.c_mem[vmm.ipc.id]; /* ... */
  mi.hblkhd   = vmm.ipc.maxpages; /* high water mark for loaded syspages */
  mi.keepcost = vmm.numpages;     /* syspages allocated */

  return mi;
}
SBMA_EXPORT(default, struct mallinfo
__sbma_mallinfo(void));


/****************************************************************************/
/*! Return some timing statistics */
/****************************************************************************/
SBMA_EXTERN struct sbma_timeinfo
__sbma_timeinfo(void)
{
  struct sbma_timeinfo ti;

  memset(&ti, 0, sizeof(struct sbma_timeinfo));

  ti.tv_rd = vmm.tmrrd;
  ti.tv_wr = vmm.tmrwr;
  //ti.tv_ad = vmm.tmrad;
  //ti.tv_ev = vmm.tmrev;

  return ti;
}
SBMA_EXPORT(default, struct sbma_timeinfo
__sbma_timeinfo(void));


/****************************************************************************/
/*! Inform runtime to release a memory blocked process. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_release(void)
{
  return __ipc_release(&(vmm.ipc));
}
SBMA_EXPORT(default, int
__sbma_release(void));
