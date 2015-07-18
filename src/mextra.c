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
#include <string.h> /* memset */
#include "common.h"
#include "ipc.h"
#include "lock.h"
#include "sbma.h"
#include "vmm.h"


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
    vmm.opts = __value;
    break;

    default:
    return -1;
  }

  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    return -1;

  return 0;
}
SBMA_EXPORT(default, int
__sbma_mallopt(int const __param, int const __value));


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
    mi.hblks = vmm.ipc.pmem[vmm.ipc.id]; /* ... */
  mi.hblkhd   = vmm.ipc.maxpages; /* high water mark for loaded syspages */
  mi.keepcost = vmm.numpages;     /* syspages allocated */

  return mi;
}
SBMA_EXPORT(default, struct mallinfo
__sbma_mallinfo(void));


/****************************************************************************/
/*! Check if a SIGIPC was received during last blocking hook. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_sigrecvd(void)
{
  return __ipc_sigrecvd(&(vmm.ipc));
}
SBMA_EXPORT(default, int
__sbma_sigrecvd(void));
