/*
Copyright (c) 2015 Jeremy Iverson

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


#include <errno.h>     /* errno */
#include <fcntl.h>     /* O_RDWR, O_CREAT, O_EXCL */
#include <signal.h>    /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t, SIZE_MAX */
#include <stdio.h>     /* FILENAME_MAX */
#include <sys/mman.h>  /* mmap, munmap */
#include <sys/stat.h>  /* S_IRUSR, S_IWUSR */
#include <sys/types.h> /* ftruncate */
#include <unistd.h>    /* ftruncate */
#include "common.h"
#include "ipc.h"
#include "sbma.h"


/*****************************************************************************/
/* Length of the IPC shared memory region. */
/*****************************************************************************/
#define IPC_LEN(N_PROCS)\
  (sizeof(size_t)+(N_PROCS)*(sizeof(int)+sizeof(size_t)+sizeof(size_t)+\
    sizeof(uint8_t))+sizeof(int))


/*****************************************************************************/
/* X Macro list. */
/*****************************************************************************/
#define LIST_OF_SEMAPHORES \
do {\
  X(inter_mtx, 1)\
  X(done, 0)\
  X(sid, 1)\
  X(sig, 0)\
} while (0);


/*****************************************************************************/
/* Constructs which implement an inter-process critical section. */
/*****************************************************************************/
#define IPC_INTER_CRITICAL_SECTION_BEG(IPC)\
do {\
  int ret;\
  ret = sem_wait((IPC)->inter_mtx);\
  ASSERT(0 == ret);\
} while (0)

#define IPC_INTER_CRITICAL_SECTION_END(IPC)\
do {\
  int ret;\
  ret = sem_post((IPC)->inter_mtx);\
  ASSERT(0 == ret);\
} while (0)


/*****************************************************************************/
/* Constructs which implement an intra-process critical section. */
/*****************************************************************************/
#define IPC_INTRA_CRITICAL_SECTION_BEG(IPC)\
do {\
  int ret;\
  ret = pthread_mutex_lock(&((IPC)->intra_mtx));\
  ASSERT(0 == ret);\
} while (0)

#define IPC_INTRA_CRITICAL_SECTION_END(IPC)\
do {\
  int ret;\
  ret = pthread_mutex_unlock(&((IPC)->intra_mtx));\
  ASSERT(0 == ret);\
} while (0)


/*****************************************************************************/
/*  MP-Safe                                                                  */
/*  MT-Invalid                                                               */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  This function MUST be called EXACTLY ONCE BEFORE any other ipc_*   */
/*        function is called.                                                */
/*****************************************************************************/
SBMA_EXTERN int
ipc_init(struct ipc * const ipc, int const uniq, int const n_procs,
         size_t const max_mem)
{
  int ret, shm_fd, id;
  void * shm;
  sem_t * inter_mtx, * done, * sid, * sig;
  int * idp;
  char fname[FILENAME_MAX];

  /* initialize semaphores */
  #define X(SEM, INIT)\
    if (0 > snprintf(fname, FILENAME_MAX, "/ipc-" #SEM "-%d", uniq))\
      return -1;\
    SEM = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, INIT);\
    if (SEM_FAILED == SEM)\
      return -1;
  LIST_OF_SEMAPHORES
  #undef X

  /* set up thread mutex */
  ret = pthread_mutex_init(&(ipc->intra_mtx), NULL);
  if (-1 == ret)
    return -1;
  /* try to create a new shared memory region -- if i create, then i should
   * also truncate it, if i dont create, then try and just open it. */
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-shm-%d", uniq))
    return -1;

  /* Set up shared memory region. */
  shm_fd = shm_open(fname, O_RDWR|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);
  if (-1 == shm_fd) {
    if (EEXIST == errno) {
      shm_fd = shm_open(fname, O_RDWR, S_IRUSR|S_IWUSR);
      if (-1 == shm_fd)
        return -1;
    }
    else {
      return -1;
    }
  }
  else {
    ret = ftruncate(shm_fd, IPC_LEN(n_procs));
    if (-1 == ret)
      return -1;

    /* initialize system memory counter */
    ret = write(shm_fd, &max_mem, sizeof(size_t));
    if (-1 == ret)
      return -1;
  }

  /* Map the shared memory region into my address space. */
  shm = mmap(NULL, IPC_LEN(n_procs), PROT_READ|PROT_WRITE, MAP_SHARED,\
    shm_fd, 0);
  if (MAP_FAILED == shm)
    return -1;

  /* Close the file descriptor. */
  ret = close(shm_fd);
  if (-1 == ret)
    return -1;

  /* Begin critical section. */
  ret = sem_wait(sid);
  if (-1 == ret)
    return -1;

  /* id pointer is last sizeof(int) bytes of shm */
  idp = (int*)((uintptr_t)shm+sizeof(size_t)+\
    (n_procs*(sizeof(int)+sizeof(size_t)+sizeof(size_t))));
  id = (*idp)++;

  /* End critical section. */
  ret = sem_post(sid);
  if (-1 == ret)
    return -1;

  /* Sanity check. */
  ASSERT(id < n_procs);

  /* Setup ipc struct. */
  ipc->id        = id;
  ipc->n_procs   = n_procs;
  ipc->uniq      = uniq;
  ipc->curpages  = 0;
  ipc->maxpages  = 0;
  ipc->shm       = shm;
  ipc->inter_mtx = inter_mtx;
  ipc->done      = done;
  ipc->sid       = sid;
  ipc->sig       = sig;
  ipc->s_mem     = (size_t*)shm;
  ipc->c_mem     = (size_t*)((uintptr_t)ipc->s_mem+sizeof(size_t));
  ipc->d_mem     = (size_t*)((uintptr_t)ipc->c_mem+(n_procs*sizeof(size_t)));
  ipc->pid       = (int*)((uintptr_t)ipc->d_mem+(n_procs*sizeof(size_t)));
  ipc->flags     = (uint8_t*)((uintptr_t)idp+sizeof(int));

  /* Set my process id. */
  ipc->pid[id] = (int)getpid();

  return 0;
}
SBMA_EXPORT(internal, int
ipc_init(struct ipc * const ipc, int const uniq, int const n_procs,
         size_t const max_mem));


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
SBMA_EXPORT(internal, int
ipc_destroy(struct ipc * const ipc));


/*****************************************************************************/
/*  MP-Unsafe race:rw(ipc->flags[ipc->id])                                   */
/*  MT-Unsafe race:rw(ipc->flags[ipc->id])                                   */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  None. ipc->flags[ipc->id] is shared by all threads in a process,   */
/*        so its value should be updated by a SINGLE thread whenever its     */
/*        status changes.                                                    */
/*****************************************************************************/
SBMA_EXTERN void
ipc_sigon(struct ipc * const ipc)
{
  ipc->flags[ipc->id] |= IPC_SIGON;
}
SBMA_EXPORT(internal, void
ipc_sigon(struct ipc * const ipc));


/*****************************************************************************/
/*  MP-Unsafe race:rw(ipc->flags[ipc->id])                                   */
/*  MT-Unsafe race:rw(ipc->flags[ipc->id])                                   */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  See note in ipc_sigon().                                           */
/*****************************************************************************/
SBMA_EXTERN void
ipc_sigoff(struct ipc * const ipc)
{
  ipc->flags[ipc->id] &= ~IPC_SIGON;
}
SBMA_EXPORT(internal, void
ipc_sigoff(struct ipc * const ipc));


/*****************************************************************************/
/*  MP-Unsafe race:rd(ipc->c_mem[id],ipc->flags[id])                         */
/*  MT-Unsafe race:rd(ipc->c_mem[id],ipc->flags[id])                         */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  Call from within an IPC_INTER_CRITICAL_SECTION or call after       */
/*        receiving SIGIPC from a process in an IPC_INTER_CRITICAL_SECTION.  */
/*****************************************************************************/
SBMA_EXTERN int
ipc_is_eligible(struct ipc * const ipc, int const id)
{
  int eligible;

  /* Default to ineligible. */
  eligible = 0;

  /* Check if anything memory is resident. */
  eligible |= ipc->c_mem[id];

  /* Check if process has enabled signaling. */
  eligible |= (ipc->flags[id]&IPC_SIGON);

  return eligible;
}
SBMA_EXPORT(internal, int
ipc_is_eligible(struct ipc * const ipc, int const id));


/*****************************************************************************/
/*  MP-Unsafe race:rw(ipc->s_mem,ipc->c_mem[ipc->id])                        */
/*  MT-Unsafe race:rw(ipc->c_mem[ipc->id],ipc->maxpages)                     */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  Call from within an IPC_INTER_CRITICAL_SECTION or call after       */
/*        receiving SIGIPC from a process in an IPC_INTER_CRITICAL_SECTION.  */
/*****************************************************************************/
SBMA_EXTERN void
ipc_atomic_inc(struct ipc * const ipc, size_t const value)
{
  ASSERT(*ipc->s_mem >= value);

  *ipc->s_mem -= value;
  ipc->c_mem[ipc->id] += value;

  if (ipc->c_mem[ipc->id] > ipc->maxpages)
    ipc->maxpages = ipc->c_mem[ipc->id];
}
SBMA_EXPORT(internal, void
ipc_atomic_inc(struct ipc * const ipc, size_t const value));


/*****************************************************************************/
/*  MP-Unsafe race:rw(ipc->s_mem,ipc->c_mem[ipc->id])                        */
/*            race:wr(ipc->d_mem[ipc->id])                                   */
/*  MT-Unsafe race:rw(ipc->s_mem,ipc->c_mem[ipc->id])                        */
/*                                                                           */
/*  Note:                                                                    */ 
/*    1)  Only other threads from this process will ever modify              */
/*        ipc->d_mem[ipc->id], so the IPC_INTRA_CRICITAL_SECTION is          */
/*        sufficient to make that variable MT-Safe.                          */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  Call from within an IPC_INTER_CRITICAL_SECTION or call after       */
/*        receiving SIGIPC from a process in an IPC_INTER_CRITICAL_SECTION.  */
/*    2)  Functions that READ ipc->d_mem[ipc->id] from a different process   */
/*        SHOULD be aware of the possibility of reading a stale value.       */
/*****************************************************************************/
SBMA_EXTERN void
ipc_atomic_dec(struct ipc * const ipc, size_t const c_pages,
               size_t const d_pages)
{
  /*=========================================================================*/
  IPC_INTRA_CRITICAL_SECTION_BEG(ipc);
  /*=========================================================================*/

  ASSERT(ipc->c_mem[ipc->id] >= c_pages);
  ASSERT(ipc->d_mem[ipc->id] >= d_pages);

  *ipc->s_mem += c_pages;
  ipc->c_mem[ipc->id] -= c_pages;
  ipc->d_mem[ipc->id] -= d_pages;

  /*=========================================================================*/
  IPC_INTRA_CRITICAL_SECTION_END(ipc);
  /*=========================================================================*/
}
SBMA_EXPORT(internal, void
ipc_atomic_dec(struct ipc * const ipc, size_t const c_pages,
               size_t const d_pages));


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
  volatile uint8_t * flags;
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
  flags   = ipc->flags;

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
SBMA_EXPORT(internal, int
ipc_madmit(struct ipc * const ipc, size_t const value,
           int const admitd));


/*****************************************************************************/
/*  MP-Safe                                                                  */
/*  MT-Safe                                                                  */
/*****************************************************************************/
SBMA_EXTERN int
ipc_mevict(struct ipc * const ipc, size_t const c_pages, size_t const d_pages)
{
  int ret;

  if (0 == c_pages && 0 == d_pages)
    return 0;

  /*=========================================================================*/
  IPC_INTER_CRITICAL_SECTION_BEG(ipc);
  /*=========================================================================*/

  ipc_atomic_dec(ipc, c_pages, d_pages);

  /*=========================================================================*/
  IPC_INTER_CRITICAL_SECTION_END(ipc);
  /*=========================================================================*/

  return 0;
}
SBMA_EXPORT(internal, int
ipc_mevict(struct ipc * const ipc, size_t const c_pages,
           size_t const d_pages));


/*****************************************************************************/
/*  MP-Unsafe race:wr(ipc->d_mem[ipc->id])                                   */
/*  MT-Safe                                                                  */
/*                                                                           */
/*  Note:                                                                    */ 
/*    1)  Only other threads from this process will ever modify              */
/*        ipc->d_mem[ipc->id], so the IPC_INTRA_CRICITAL_SECTION is          */
/*        sufficient to make that variable MT-Safe.                          */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  Functions that READ ipc->d_mem[ipc->id] from a different process   */
/*        SHOULD be aware of the possibility of reading a stale value.       */
/*****************************************************************************/
SBMA_EXTERN int
ipc_mdirty(struct ipc * const ipc, ssize_t const value)
{
  if (0 == value)
    return 0;

  /*=========================================================================*/
  IPC_INTRA_CRITICAL_SECTION_BEG(ipc);
  /*=========================================================================*/

  if (value < 0) {
    ASSERT(ipc->d_mem[ipc->id] >= value);
  }

  ipc->d_mem[ipc->id] += value;

  /*=========================================================================*/
  IPC_INTRA_CRITICAL_SECTION_END(ipc);
  /*=========================================================================*/

  return 0;
}
SBMA_EXPORT(internal, int
ipc_mdirty(struct ipc * const ipc, ssize_t const value));
