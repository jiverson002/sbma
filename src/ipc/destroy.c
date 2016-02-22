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


#include <errno.h>     /* errno library */
#include <stdio.h>     /* FILENAME_MAX */
#include <sys/mman.h>  /* munmap */
#include "common.h"
#include "ipc.h"
#include "sbma.h"


/*****************************************************************************/
/*  MP-Safe                                                                  */
/*  MT-Invalid                                                               */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  This function MUST be called EXACTLY ONCE AFTER all other ipc_*    */
/*        functions are called.                                              */
/*****************************************************************************/
SBMA_EXTERN int
ipc_destroy(struct ipc * const ipc)
{
  /* No need for intra critical section here because this should only be called
   * from the ``main'' process, never from threads. */

  int ret;
  char fname[FILENAME_MAX];

  ipc->curpages = ipc->c_mem[ipc->id];

  ret = pthread_mutex_destroy(&(ipc->intra_mtx));
  if (-1 == ret)
    return -1;

  ret = munmap((void*)ipc->shm, IPC_LEN(ipc->n_procs));
  if (-1 == ret)
    return -1;

  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-shm-%d", ipc->uniq))
    return -1;
  ret = shm_unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;

  #define X(NAME, ...)\
    if (0 > snprintf(fname, FILENAME_MAX, "/ipc-" #NAME "-%d", ipc->uniq))\
      return -1;\
    ret = sem_close(ipc->NAME);\
    if (-1 == ret)\
      return -1;\
    ret = sem_unlink(fname);\
    if (-1 == ret && ENOENT != errno)\
      return -1;
  LIST_OF_SEMAPHORES
  #undef X

  return 0;
}
