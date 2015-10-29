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


/****************************************************************************/
/*! Constructs which implement a inter-process critical section. */
/****************************************************************************/
#define INTER_CRITICAL_SECTION_BEG(IPC)\
do {\
  int ret;\
  ret = sem_wait((IPC)->inter_mtx);\
  if (-1 == ret) {\
    goto INTER_CRITICAL_SECTION_ERREXIT;\
  }\
} while (0)

#define INTER_CRITICAL_SECTION_END(IPC)\
do {\
  int ret;\
  ret = sem_post((IPC)->inter_mtx);\
  if (-1 == ret){\
    goto INTER_CRITICAL_SECTION_ERREXIT;\
  }\
  goto INTER_CRITICAL_SECTION_DONE;\
  INTER_CRITICAL_SECTION_ERREXIT:\
  ASSERT(0);\
  INTER_CRITICAL_SECTION_DONE:\
  (void)0;\
} while (0)


/****************************************************************************/
/*! Constructs which implement a intra-process critical section. */
/****************************************************************************/
#define INTRA_CRITICAL_SECTION_BEG(IPC)\
do {\
  int ret;\
  ret = pthread_mutex_lock(&((IPC)->intra_mtx));\
  if (-1 == ret) {\
    goto INTRA_CRITICAL_SECTION_ERREXIT;\
  }\
} while (0)

#define INTRA_CRITICAL_SECTION_END(IPC)\
do {\
  int ret;\
  ret = pthread_mutex_unlock(&((IPC)->intra_mtx));\
  if (-1 == ret) {\
    goto INTRA_CRITICAL_SECTION_ERREXIT;\
  }\
  goto INTRA_CRITICAL_SECTION_DONE;\
  INTRA_CRITICAL_SECTION_ERREXIT:\
  ASSERT(0);\
  INTRA_CRITICAL_SECTION_DONE:\
  (void)0;\
} while (0)


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
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-inter-mtx-%d", uniq))
    return -1;
  inter_mtx = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 1);
  if (SEM_FAILED == inter_mtx)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-done-%d", uniq))
    return -1;
  done = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 0);
  if (SEM_FAILED == done)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-sid-%d", uniq))
    return -1;
  sid = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 1);
  if (SEM_FAILED == sid)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-sig-%d", uniq))
    return -1;
  sig = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 0);
  if (SEM_FAILED == sig)
    return -1;

  /* try to create a new shared memory region -- if i create, then i should
   * also truncate it, if i dont create, then try and just open it. */
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-shm-%d", uniq))
    return -1;
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

  /* map the shared memory region into my address space */
  shm = mmap(NULL, IPC_LEN(n_procs), PROT_READ|PROT_WRITE, MAP_SHARED,\
    shm_fd, 0);
  if (MAP_FAILED == shm)
    return -1;

  /* close the file descriptor */
  ret = close(shm_fd);
  if (-1 == ret)
    return -1;

  /* begin critical section */
  ret = sem_wait(sid);
  if (-1 == ret)
    return -1;

  /* id pointer is last sizeof(int) bytes of shm */
  idp = (int*)((uintptr_t)shm+sizeof(size_t)+\
    (n_procs*(sizeof(int)+sizeof(size_t)+sizeof(size_t))));
  id  = (*idp)++;

  /* end critical section */
  ret = sem_post(sid);
  if (-1 == ret)
    return -1;
  ret = sem_close(sid);
  if (-1 == ret)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-sid-%d", uniq))
    return -1;
  ret = sem_unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;

  if (id >= n_procs)
    return -1;

  /* setup ipc struct */
  ipc->init      = 1;
  ipc->id        = id;
  ipc->n_procs   = n_procs;
  ipc->uniq      = uniq;
  ipc->curpages  = 0;
  ipc->maxpages  = 0;
  ipc->shm       = shm;
  ipc->inter_mtx = inter_mtx;
  ipc->done      = done;
  ipc->sig       = sig;
  ipc->smem      = (size_t*)shm;
  ipc->c_mem     = (size_t*)((uintptr_t)ipc->smem+sizeof(size_t));
  ipc->d_mem     = (size_t*)((uintptr_t)ipc->c_mem+(n_procs*sizeof(size_t)));
  ipc->pid       = (int*)((uintptr_t)ipc->d_mem+(n_procs*sizeof(size_t)));
  ipc->flags     = (uint8_t*)((uintptr_t)idp+sizeof(int));

  /* set my process id */
  ipc->pid[id] = (int)getpid();

  /* set up thread mutex */
  ret = pthread_mutex_init(&(ipc->intra_mtx), NULL);
  if (-1 == ret)
    return -1;

  return 0;
}
SBMA_EXPORT(internal, int
ipc_init(struct ipc * const ipc, int const uniq, int const n_procs,
         size_t const max_mem));


SBMA_EXTERN int
ipc_destroy(struct ipc * const ipc)
{
  int ret;
  char fname[FILENAME_MAX];

  ipc->init = 0;

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

  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-inter-mtx-%d", ipc->uniq))
    return -1;
  ret = sem_close(ipc->inter_mtx);
  if (-1 == ret)
    return -1;
  ret = sem_unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-done-%d", ipc->uniq))
    return -1;
  ret = sem_close(ipc->done);
  if (-1 == ret)
    return -1;
  ret = sem_unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-sig-%d", ipc->uniq))
    return -1;
  ret = sem_close(ipc->sig);
  if (-1 == ret)
    return -1;
  ret = sem_unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;

  return 0;
}
SBMA_EXPORT(internal, int
ipc_destroy(struct ipc * const ipc));


SBMA_EXTERN void
ipc_sigon(struct ipc * const ipc)
{
  /* No need for intra critical section here because this should only be called
   * from the ``main'' process, never from threads. */

  if (1 == ipc->init) {
    ipc->flags[ipc->id] |= IPC_SIGON;
  }
}
SBMA_EXPORT(internal, void
ipc_sigon(struct ipc * const ipc));


SBMA_EXTERN void
ipc_sigoff(struct ipc * const ipc)
{
  /* See note in ipc_sigon. */

  if (1 == ipc->init) {
    ipc->flags[ipc->id] &= ~IPC_SIGON;
  }
}
SBMA_EXPORT(internal, void
ipc_sigoff(struct ipc * const ipc));


SBMA_EXTERN int
ipc_is_eligible(struct ipc * const ipc, int const id)
{
  /* No need for inter or intra critical section because this is only called
   * when a process is holding the inter_mutex. */

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


SBMA_EXTERN int
ipc_atomic_inc(struct ipc * const ipc, size_t const value)
{
  ASSERT(*ipc->smem >= value);

  *ipc->smem -= value;
  ipc->c_mem[ipc->id] += value;

  if (ipc->c_mem[ipc->id] > ipc->maxpages)
    ipc->maxpages = ipc->c_mem[ipc->id];

  return 0;
}
SBMA_EXPORT(internal, int
ipc_atomic_inc(struct ipc * const ipc, size_t const value));


SBMA_EXTERN int
ipc_atomic_dec(struct ipc * const ipc, size_t const c_pages,
               size_t const d_pages)
{
  ASSERT(ipc->c_mem[ipc->id] >= c_pages);
  ASSERT(ipc->d_mem[ipc->id] >= d_pages);

  *ipc->smem += c_pages;
  ipc->c_mem[ipc->id] -= c_pages;
  ipc->d_mem[ipc->id] -= d_pages;

  return 0;
}
SBMA_EXPORT(internal, int
ipc_atomic_dec(struct ipc * const ipc, size_t const c_pages,
               size_t const d_pages));


SBMA_EXTERN int
ipc_madmit(struct ipc * const ipc, size_t const value, int const admitd)
{
  int retval, ret, i, ii, id, n_procs;
  size_t mx_c_mem, mx_d_mem, smem;
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

  /*========================================================================*/
  INTER_CRITICAL_SECTION_BEG(ipc);
  /*========================================================================*/

  smem = *ipc->smem;
  while (smem < value) {
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
      if ((mx_c_mem < value-smem && c_mem[i] > mx_c_mem) ||\
          (c_mem[i] >= value-smem &&\
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
    smem = *ipc->smem;
  }

  ASSERT(smem >= value);

  ret = ipc_atomic_inc(ipc, value);
  ASSERT(0 == ret);

  /*========================================================================*/
  INTER_CRITICAL_SECTION_END(ipc);
  /*========================================================================*/

  goto RETURN;

  ERREXIT:
  retval = -1;

  RETURN:
  return retval;
}
SBMA_EXPORT(internal, int
ipc_madmit(struct ipc * const ipc, size_t const value,
           int const admitd));


SBMA_EXTERN int
ipc_mevict(struct ipc * const ipc, size_t const c_pages, size_t const d_pages)
{
  int ret;

  if (0 == c_pages && 0 == d_pages)
    return 0;

  /*========================================================================*/
  INTER_CRITICAL_SECTION_BEG(ipc);
  /*========================================================================*/

  ret = ipc_atomic_dec(ipc, c_pages, d_pages);
  ASSERT(0 == ret);

  /*========================================================================*/
  INTER_CRITICAL_SECTION_END(ipc);
  /*========================================================================*/

  return 0;
}
SBMA_EXPORT(internal, int
ipc_mevict(struct ipc * const ipc, size_t const c_pages,
           size_t const d_pages));


SBMA_EXTERN int
ipc_mdirty(struct ipc * const ipc, ssize_t const value)
{
  if (0 == value)
    return 0;

  /*------------------------------------------------------------------------*/
  /* Thread critical only because we are modifying only process owned data. */
  /*------------------------------------------------------------------------*/
  /*========================================================================*/
  INTRA_CRITICAL_SECTION_BEG(ipc);
  /*========================================================================*/

  if (value < 0) {
    ASSERT(ipc->d_mem[ipc->id] >= value);
  }

  ipc->d_mem[ipc->id] += value;

  /*========================================================================*/
  INTRA_CRITICAL_SECTION_END(ipc);
  /*========================================================================*/

  return 0;
}
SBMA_EXPORT(internal, int
ipc_mdirty(struct ipc * const ipc, ssize_t const value));
