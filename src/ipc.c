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
#include <semaphore.h> /* semaphore library */
#include <signal.h>    /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t */
#include <stdio.h>     /* FILENAME_MAX */
#include <sys/mman.h>  /* mmap, mremap, munmap, madvise, mprotect */
#include <sys/stat.h>  /* S_IRUSR, S_IWUSR */
#include <sys/types.h> /* ftruncate */
#include <time.h>      /* CLOCK_REALTIME, struct timespec, clock_gettime */
#include <unistd.h>    /* ftruncate */
#include "common.h"
#include "ipc.h"
#include "sbma.h"
#include "vmm.h"


#define CALL_LIBC(__IPC, __CMD, __SIGRECVD)\
do {\
  ret = __ipc_block(__IPC, IPC_MEM_BLOCKED);\
  if (-1 == ret)\
    return -1;\
  ret = libc_ ## __CMD;\
  if (-1 == ret) {\
    ret = __ipc_unblock(__IPC);\
    ASSERT(-1 != ret);\
    if (EINTR == errno || ETIMEDOUT == errno)\
      errno = EAGAIN;\
    return -1;\
  }\
  ret = __ipc_unblock(__IPC);\
  if (-1 == ret)\
    return -1;\
  ret = __ipc_sigrecvd(__IPC);\
  if (1 == ret) {\
    ret = __SIGRECVD;\
    ASSERT(-1 != ret);\
    errno = EAGAIN;\
    return -1;\
  }\
} while (0)


/****************************************************************************/
/*!
 * Thread static variables for checking to see if a SIGIPC was received and
 * honored bewteen __ipc_block() and __ipc_unblock().
 */
/****************************************************************************/
static __thread size_t _ipc_l_pages;
static __thread int    _ipc_sigrecvd;


SBMA_EXTERN int
__ipc_init(struct ipc * const __ipc, int const __uniq, int const __n_procs,
           size_t const __max_mem)
{
  int ret, shm_fd, id;
  void * shm;
  sem_t * mtx, * trn1, * trn2, * trn3, * sid;
  int * idp;
  char fname[FILENAME_MAX];

  /* initialize semaphores */
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-mtx-%d", __uniq))
    return -1;
  mtx = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 1);
  if (SEM_FAILED == mtx)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-trn1-%d", __uniq))
    return -1;
  trn1 = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 0);
  if (SEM_FAILED == trn1)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-trn2-%d", __uniq))
    return -1;
  trn2 = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 1);
  if (SEM_FAILED == trn2)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-trn3-%d", __uniq))
    return -1;
  trn3 = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 0);
  if (SEM_FAILED == trn3)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-sid-%d", __uniq))
    return -1;
  sid = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 1);
  if (SEM_FAILED == sid)
    return -1;

  /* try to create a new shared memory region -- if i create, then i should
   * also truncate it, if i dont create, then try and just open it. */
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-shm-%d", __uniq))
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
    ret = ftruncate(shm_fd, IPC_LEN(__n_procs));
    if (-1 == ret)
      return -1;

    /* initialize system memory counter */
    ret = write(shm_fd, &__max_mem, sizeof(size_t));
    if (-1 == ret)
      return -1;
  }

  /* map the shared memory region into my address space */
  shm = mmap(NULL, IPC_LEN(__n_procs), PROT_READ|PROT_WRITE, MAP_SHARED,\
    shm_fd, 0);
  if (MAP_FAILED == shm)
    return -1;

  /* close the file descriptor */
  ret = close(shm_fd);
  if (-1 == ret)
    return -1;

  /* begin critical section */
  ret = libc_sem_wait(sid);
  if (-1 == ret)
    return -1;

  /* id pointer is last sizeof(int) bytes of shm */
  idp = (int*)((uintptr_t)shm+sizeof(size_t)+(__n_procs*(sizeof(size_t)+sizeof(int))));
  id  = (*idp)++;

  /* end critical section */
  ret = sem_post(sid);
  if (-1 == ret)
    return -1;
  ret = sem_close(sid);
  if (-1 == ret)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-sid-%d", __uniq))
    return -1;
  ret = sem_unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;

  if (id >= __n_procs)
    return -1;

  /* setup ipc struct */
  __ipc->init     = 1;
  __ipc->id       = id;
  __ipc->n_procs  = __n_procs;
  __ipc->uniq     = __uniq;
  __ipc->curpages = 0;
  __ipc->maxpages = 0;
  __ipc->shm      = shm;
  __ipc->mtx      = mtx;
  __ipc->trn1     = trn1;
  __ipc->trn2     = trn2;
  __ipc->trn3     = trn3;
  __ipc->smem     = (size_t*)shm;
  __ipc->pmem     = (size_t*)((uintptr_t)__ipc->smem+sizeof(size_t));
  __ipc->pid      = (int*)((uintptr_t)__ipc->pmem+(__n_procs*sizeof(size_t)));
  __ipc->flags    = (uint8_t*)((uintptr_t)__ipc->pid+sizeof(int)+(__n_procs*sizeof(int)));

  /* set my process id */
  __ipc->pid[id] = (int)getpid();

  return 0;
}
SBMA_EXPORT(internal, int
__ipc_init(struct ipc * const __ipc, int const __uniq, int const __n_procs,
           size_t const __max_mem));


SBMA_EXTERN int
__ipc_destroy(struct ipc * const __ipc)
{
  int ret;
  char fname[FILENAME_MAX];

  __ipc->init = 0;

  __ipc->curpages = __ipc->pmem[__ipc->id];

  ret = munmap(__ipc->shm, IPC_LEN(__ipc->n_procs));
  if (-1 == ret)
    return -1;

  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-shm-%d", __ipc->uniq))
    return -1;
  ret = shm_unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;

  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-mtx-%d", __ipc->uniq))
    return -1;
  ret = sem_close(__ipc->mtx);
  if (-1 == ret)
    return -1;
  ret = sem_unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-trn1-%d", __ipc->uniq))
    return -1;
  ret = sem_close(__ipc->trn1);
  if (-1 == ret)
    return -1;
  ret = sem_unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-trn2-%d", __ipc->uniq))
    return -1;
  ret = sem_close(__ipc->trn2);
  if (-1 == ret)
    return -1;
  ret = sem_unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-trn3-%d", __ipc->uniq))
    return -1;
  ret = sem_close(__ipc->trn3);
  if (-1 == ret)
    return -1;
  ret = sem_unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;

  return 0;
}
SBMA_EXPORT(internal, int
__ipc_destroy(struct ipc * const __ipc));


SBMA_EXTERN int
__ipc_block(struct ipc * const __ipc, int const __flag)
{
#if SBMA_VERSION >= 200
  int ret, sval;
#endif

  if (0 == __ipc->init)
    return 0;

  /* Get number of loaded pages when process is in a state where reception of
   * SIGIPC is not honored. */
  _ipc_l_pages  = __ipc->pmem[__ipc->id];
  _ipc_sigrecvd = 0;

#if SBMA_VERSION >= 200
#if 1
  if (IPC_CMD_BLOCKED == __flag) {
    ret = libc_sem_wait(__ipc->trn2);
    if (-1 == ret)
      return -1;

    ret = sem_getvalue(__ipc->trn3, &sval);
    if (-1 == ret)
      return -1;

    if (0 == sval) {
      ret = sem_post(__ipc->trn3);
      if (-1 == ret)
        return -1;
    }

    ret = sem_post(__ipc->trn2);
    if (-1 == ret)
      return -1;
  }
#endif
#endif

  /* Transition to blocked */
  __ipc->flags[__ipc->id] |= __flag;

  return 0;
}
SBMA_EXPORT(internal, int
__ipc_block(struct ipc * const __ipc, int const __flag));


SBMA_EXTERN int
__ipc_unblock(struct ipc * const __ipc)
{
  if (0 == __ipc->init)
    return 0;

  /* Transition to running */
  __ipc->flags[__ipc->id] &= ~(IPC_CMD_BLOCKED|IPC_MEM_BLOCKED);

  /* Compare number of loaded pages now, when process is in a state where
   * reception of SIGIPC is not honored, to before the process was put in
   * state where it would be honored. If the two values are different, then a
   * SIGIPC was received and honored and thus __ipc_sigrecvd should be set to
   * 1. */
  if (_ipc_l_pages != __ipc->pmem[__ipc->id])
    _ipc_sigrecvd = 1;

  return 0;
}
SBMA_EXPORT(internal, int
__ipc_unblock(struct ipc * const __ipc));


SBMA_EXTERN int
__ipc_populate(struct ipc * const __ipc)
{
  if (0 == __ipc->init)
    return 0;
  __ipc->flags[__ipc->id] |= IPC_POPULATED;
  return 0;
}
SBMA_EXPORT(internal, int
__ipc_populate(struct ipc * const __ipc));


SBMA_EXTERN int
__ipc_unpopulate(struct ipc * const __ipc)
{
  if (0 == __ipc->init)
    return 0;
  __ipc->flags[__ipc->id] &= ~IPC_POPULATED;
  return 0;
}
SBMA_EXPORT(internal, int
__ipc_unpopulate(struct ipc * const __ipc));


SBMA_EXTERN int
__ipc_sigrecvd(struct ipc * const __ipc)
{
  if (NULL == __ipc) {}

  return _ipc_sigrecvd;
}
SBMA_EXPORT(internal, int
__ipc_sigrecvd(struct ipc * const __ipc));


SBMA_EXTERN int
__ipc_is_eligible(struct ipc * const __ipc)
{
  uint8_t const flag1 = (IPC_CMD_BLOCKED|IPC_POPULATED);
  uint8_t const flag2 = (IPC_MEM_BLOCKED|IPC_POPULATED);
  volatile uint8_t const flags = __ipc->flags[__ipc->id];
  if (0 == __ipc->init)
    return 0;
  return ((flag1 == (flags&flag1)) || (flag2 == (flags&flag2)));
}
SBMA_EXPORT(internal, int
__ipc_is_eligible(struct ipc * const __ipc));


SBMA_EXTERN int
__ipc_madmit(struct ipc * const __ipc, size_t const __value)
{
  /* TODO: There is some potential for optimization here regarding how and
   * when processes are chosen for eviction. For example, instead of choosing
   * to evict the process with the most resident memory, we could chose the
   * process with the least or the process with the least, but still greater
   * than the request. Another example is instead of blindly evicting
   * processes, even if their resident memory will not satisfy the request, we
   * can choose not to evict processes unless the eviction will successfully
   * satisfy the request.
   */

  int ret, i, ii, id, dlctr;
  size_t mxmem, smem;
  volatile uint8_t * flags;
  int * pid;
  volatile size_t * pmem;
#if SBMA_VERSION >= 200
  int sval;
  struct timespec ts;
#endif

  if (0 == __value)
    return 0;

  CALL_LIBC(__ipc, sem_wait(__ipc->mtx), sem_post(__ipc->mtx));

  id    = __ipc->id;
  smem  = *__ipc->smem;
  pmem  = __ipc->pmem;
  pid   = __ipc->pid;
  flags = __ipc->flags;

  while (smem < __value) {
    /* find a process which has memory and is eligible */
    mxmem = 0;
    ii    = -1;
    for (dlctr=0,i=0; i<__ipc->n_procs; ++i) {
      dlctr += (IPC_CMD_BLOCKED == (flags[i]&IPC_CMD_ELIGIBLE));
      dlctr += (IPC_MEM_BLOCKED == (flags[i]&IPC_MEM_ELIGIBLE));

      if (i == id)
        continue;

#if 1
      /* If the process is blocked on a function call, then choose it as a
       * candidate if it has the most resident memory so far. */
      if (IPC_CMD_ELIGIBLE == (flags[i]&IPC_CMD_ELIGIBLE)) {
        if (pmem[i] > mxmem) {
          ii = i;
          mxmem = pmem[i];
        }
      }
      /* If the process is blocked trying to admit memory, then choose it only
       * if has the most resident memory so far AND my resident memory is
       * larger than its resident memory. */
      else if (IPC_MEM_ELIGIBLE == (flags[i]&IPC_MEM_ELIGIBLE)) {
        if (pmem[i] > mxmem && pmem[id] >= pmem[i]) {
          ii = i;
          mxmem = pmem[i];
        }
      }
#else
      if (IPC_CMD_ELIGIBLE == (flags[i]&IPC_CMD_ELIGIBLE) ||\
        IPC_MEM_ELIGIBLE == (flags[i]&IPC_MEM_ELIGIBLE))
      {
        if (pmem[i] > mxmem) {
          ii = i;
          mxmem = pmem[i];
        }
      }
#endif
    }

    /* no such process exists, break loop */
    if (-1 == ii) {
      if (dlctr == __ipc->n_procs-1) {
        printf("[%5d] {%zu,%zu} (", (int)getpid(), __value, *__ipc->smem);
        for (i=0; i<__ipc->n_procs; ++i)
          printf(" <%d,%d,%zu>", pid[i], flags[i], pmem[i]);
        printf(" )\n");
      }
      ASSERT(dlctr != __ipc->n_procs-1);
      break;
    }

    /* such a process is available, tell it to free memory */
    ret = kill(pid[ii], SIGIPC);
    if (-1 == ret)
      goto CLEANUP1;

    /* wait for it to signal it has finished */
    ret = libc_sem_wait(__ipc->trn1);
    if (-1 == ret) {
      ASSERT(EINTR != errno);
      goto CLEANUP1;
    }

    smem = *__ipc->smem;
  }

#if SBMA_VERSION >= 200
  if (smem < __value) {
    ret = sem_post(__ipc->mtx);
    if (-1 == ret)
      goto CLEANUP1;

#if 0
    /* This approach is solely used as an early alert method. This means that
     * instead of requiring the process to sleep for the whole period, it can
     * be signalled early if another process enters the CMD_BLOCKED state. */
    ret = clock_gettime(CLOCK_REALTIME, &ts);
    if (-1 == ret)
      goto ERREXIT;
    if ((long)999999999-ts.tv_nsec < (long)100000000) {
      ts.tv_sec++;
      ts.tv_nsec = (long)100000000-((long)999999999-ts.tv_nsec);
    }
    else {
      ts.tv_nsec += 100000000;
    }

    /* Cannot post to trn3 in case of sigrecvd, because doing so could
     * possibly break the sem_getvalue() reliance in __ipc_block(). */
    CALL_LIBC(__ipc, sem_timedwait(__ipc->trn3, &ts), 0);
#elif 1
    ts.tv_sec  = 0;
    ts.tv_nsec = 50000000;

    for (;;) {
      CALL_LIBC(__ipc, sem_wait(__ipc->trn2), sem_post(__ipc->trn2));

      ret = sem_getvalue(__ipc->trn3, &sval);
      if (-1 == ret)
        goto CLEANUP3;

      if (1 == sval) {
        ret = libc_sem_wait(__ipc->trn3);
        if (-1 == ret)
          goto CLEANUP3;

        ret = sem_post(__ipc->trn2);
        if (-1 == ret)
          goto CLEANUP3;

        break;
      }

      ret = sem_post(__ipc->trn2);
      if (-1 == ret)
        goto CLEANUP3;

      CALL_LIBC(__ipc, nanosleep(&ts, NULL), 0);
    }
#else
    ts.tv_sec  = 0;
    ts.tv_nsec = 100000000;
    CALL_LIBC(__ipc, nanosleep(&ts, NULL), 0);
#endif

    ASSERT(0 == __ipc_is_eligible(__ipc));
    errno = EAGAIN;
    goto ERREXIT;
  }
  else {
#endif
    *__ipc->smem -= __value;
    __ipc->pmem[id] += __value;
    ret = libc_msync(__ipc->shm, IPC_LEN(__ipc->n_procs), MS_SYNC);
    if (-1 == ret)
      goto CLEANUP2;

    if (__ipc->pmem[id] > __ipc->maxpages)
      __ipc->maxpages = __ipc->pmem[id];

    ret = __ipc_populate(__ipc);
    if (-1 == ret)
      goto CLEANUP2;

    ret = sem_post(__ipc->mtx);
    if (-1 == ret)
      goto CLEANUP1;

    ASSERT(smem >= __value);
    ASSERT(0 == __ipc_is_eligible(__ipc));
    return 0;
#if SBMA_VERSION >= 200
  }
#endif

#if SBMA_VERSION >= 200
#if 1
  CLEANUP3:
  ret = sem_post(__ipc->trn2);
  ASSERT(-1 != ret);
  goto ERREXIT;
#endif
#endif
  CLEANUP2:
  *__ipc->smem += __value;
  __ipc->pmem[id] -= __value;
  ret = libc_msync(__ipc->shm, IPC_LEN(__ipc->n_procs), MS_SYNC);
  ASSERT(-1 != ret);
  CLEANUP1:
  ret = sem_post(__ipc->mtx);
  ASSERT(-1 != ret);
#if SBMA_VERSION >= 200
  ERREXIT:
#endif
  return -1;
}
SBMA_EXPORT(internal, int
__ipc_madmit(struct ipc * const __ipc, size_t const __value));


SBMA_EXTERN int
__ipc_mevict(struct ipc * const __ipc, size_t const __value)
{
  int ret;

  if (0 == __value)
    return 0;

  ASSERT(0 == __ipc_is_eligible(__ipc));

#if 1
  CALL_LIBC(__ipc, sem_wait(__ipc->mtx), sem_post(__ipc->mtx));
#else
  ret = sem_wait(__ipc->mtx);
  if (-1 == ret) {
    if (EINTR == errno)
      errno = EAGAIN;
    return -1;
  }
  else if (1 == __ipc_sigrecvd(__ipc)) {
    /* Need to release semaphore since this is returning an error. */
    (void)sem_post(__ipc->mtx);
    errno = EAGAIN;
    return -1;
  }
#endif

  ASSERT(__ipc->pmem[__ipc->id] >= __value);

  *__ipc->smem += __value;
  __ipc->pmem[__ipc->id] -= __value;
  ret = libc_msync((void*)__ipc->smem, sizeof(size_t), MS_SYNC);
  if (-1 == ret)
    goto ERREXIT;

  ret = sem_post(__ipc->mtx);
  if (-1 == ret)
    goto ERREXIT;

  ASSERT(0 == __ipc_is_eligible(__ipc));
  return 0;

  ERREXIT:
  return -1;
}
SBMA_EXPORT(internal, int
__ipc_mevict(struct ipc * const __ipc, size_t const __value));
