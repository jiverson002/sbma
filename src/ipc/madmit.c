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


#include <signal.h> /* kill */
#include <stddef.h> /* size_t, SIZE_MAX */
#include "common.h"
#include "ipc.h"
#include "sbma.h"


/*****************************************************************************/
/*  MP-Unsafe race:rd(ipc->d_mem[ipc->id])                                   */
/*  MT-Unsafe race:rd(ipc->d_mem[ipc->id])                                   */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  Function is designed such that if a stale ipc->d_mem[ipc->id]      */
/*        value is read, then resulting execution will still be correct.     */
/*        Only performance will be impacted, likely negatively.              */
/*****************************************************************************/
SBMA_EXTERN int
ipc_madmit(struct ipc * const ipc, size_t const value, int const admitd)
{
  int retval, ret, i, ii, id, n_procs;
  size_t mx_c_mem, mx_d_mem, s_mem;
  int * pid;
  volatile size_t * c_mem, * d_mem;

  /* Default return value is success. */
  retval = 0;

  /* Shortcut. */
  if (0 == value)
    goto RETURN;

  id      = ipc->id;
  n_procs = ipc->n_procs;
  c_mem   = ipc->c_mem;
  d_mem   = ipc->d_mem;
  pid     = ipc->pid;

  /*=========================================================================*/
  IPC_INTER_CRITICAL_SECTION_BEG(ipc);
  /*=========================================================================*/

  s_mem = *ipc->s_mem;
  while (s_mem < value) {
    ii       = -1;
    mx_c_mem = 0;
    mx_d_mem = SIZE_MAX;

    /* Find a candidate process to release memory. */
    for (i=0; i<n_procs; ++i) {
      /* Skip oneself. */
      if (i == id) {
        continue;
      }
      /* Skip process which are ineligible. */
      else if (!ipc_is_eligible(ipc, i)) {
        continue;
      }

      /*
       *  Choose the process to evict as follows:
       *    1) If no candidate process has resident memory greater than the
       *       requested memory, then choose the candidate which has the most
       *       resident memory.
       *    2) If some candidate process(es) have resident memory greater than
       *       the requested memory, then:
       *       2.1) If VMM_ADMITD != admitd, then choose from these, the
       *            candidate which has the least resident memory.
       *       2.2) If VMM_ADMITD == admitd, then choose from these, the
       *            candidate which has the least dirty memory.
       */
      if ((mx_c_mem < value-s_mem && c_mem[i] > mx_c_mem) ||\
          (c_mem[i] >= value-s_mem &&\
            ((VMM_ADMITD != admitd && c_mem[i] < mx_c_mem) ||\
             (VMM_ADMITD == admitd && d_mem[i] < mx_d_mem))))
      {
        ii = i;
        mx_c_mem = c_mem[i];
        mx_d_mem = d_mem[i];
      }
    }

    /* No valid candidate process exists, retry loop in case a stale value was
     * read. */
    if (-1 == ii) {
      continue;
    }

    /* Tell the chosen candidate process to free memory. */
    ret = kill(pid[ii], SIGIPC);
    if (-1 == ret) {
      goto ERREXIT;
    }

    /* Wait for it to signal it has finished. */
    ret = sem_wait(ipc->done);
    if (-1 == ret) {
      goto ERREXIT;
    }

    /* Re-cache system memory value. */
    s_mem = *ipc->s_mem;
  }

  ASSERT(s_mem >= value);

  ipc_atomic_inc(ipc, value);

  /*=========================================================================*/
  IPC_INTER_CRITICAL_SECTION_END(ipc);
  /*=========================================================================*/

  goto RETURN;

  ERREXIT:
  retval = -1;

  RETURN:
  return retval;
}


#ifdef TEST
int
main(int argc, char * argv[])
{
  if (0 == argc || NULL == argv) {}

  return 0;
}
#endif
