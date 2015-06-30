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


#ifdef NDEBUG
# undef NDEBUG
#endif


#include <malloc.h> /* struct mallinfo */
#include <string.h> /* memset */
#include "config.h"
#include "sbma.h"
#include "ipc.h"
#include "vmm.h"


/****************************************************************************/
/*! Set parameters for the sbmalloc subsystem. */
/****************************************************************************/
extern int
__ooc_mallopt__(int const __param, int const __value)
{
  int ret;

  ret = LOCK_GET(&(vmm.lock));
  if (-1 == ret)
    return -1;

  switch (__param) {
    case M_VMMOPTS:
    vmm.opts = __value;
    break;

    default:
    return -1;
  }

  ret = LOCK_LET(&(vmm.lock));
  if (-1 == ret)
    return -1;

  return 0;
}


/****************************************************************************/
/*! Return some memory statistics */
/****************************************************************************/
extern struct mallinfo
__ooc_mallinfo__(void)
{
  int ret;
  struct mallinfo mi;

  memset(&mi, 0, sizeof(struct mallinfo));

  /* Not checking the return value here is a hack which allows this function
   * to be called even after __vmm_destroy__() has been called. */
  /*ret = LOCK_GET(&(vmm.lock));
  if (-1 == ret)
    return mi;*/
  (void)LOCK_GET(&(vmm.lock));

  mi.smblks  = vmm.numrf; /* read faults */
  mi.ordblks = vmm.numwf; /* write faults */

  mi.usmblks = vmm.numrd; /* syspages read from disk */
  mi.fsmblks = vmm.numwr; /* syspages wrote to disk */

  mi.uordblks = vmm.curpages; /* syspages loaded */
  mi.fordblks = vmm.maxpages; /* high water mark for loaded syspages */
  mi.keepcost = vmm.numpages; /* syspages allocated */

  /* Not checking the return value here is a hack which allows this function
   * to be called even after __vmm_destroy__() has been called. */
  /*ret = LOCK_LET(&(vmm.lock));
  if (-1 == ret)
    return mi;*/
  (void)LOCK_LET(&(vmm.lock));

  return mi;
}


/****************************************************************************/
/*! Modify the eligibility of a process for ipc memory tracking */
/****************************************************************************/
extern int
__ooc_eligible__(int const __eligible)
{
  if (0 == vmm.init)
    return 0;

  return __ipc_eligible__(&(vmm.ipc), __eligible);
}


/****************************************************************************/
/*! Modify the eligibility of a process for ipc memory tracking */
/****************************************************************************/
extern int
__ooc_is_eligible__(void)
{
  if (0 == vmm.init)
    return 0;

  return __ipc_is_eligible__(&(vmm.ipc));
}
