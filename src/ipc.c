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


#if SBMA_VERSION >= 200
# define SBMA_WAIT_ALGO 0  /* timed wait on trn3 / manual posts */
//# define SBMA_WAIT_ALGO 1  /* wait on trn3 or all blocked / manual posts */
//# define SBMA_WAIT_ALGO 2  /* timed sleep */
#else
# define SBMA_WAIT_ALGO (-1) /* disable all wait algorithm code */
#endif


/****************************************************************************/
/*! Thread-local variable to check for signal received. */
/* TODO: What happens if two threads are admitting, one thread receives a
 * signal, then shouldn't the other thread also be aware that the process has
 * received a signal, since its memory (which is shared with the other
 * thread), will also have been evicted. This could easily be implemented by
 * using a non-thread-local variable. However, then care must be taken that a
 * thread entering the blocked state (setting ipc_sigrecvd to zero) does not
 * overwrite a non-zero value. */
/* TODO: A possible solution to this could be to have a counter to see how
 * many threads are blocked, then if the counter is non-zero, do not reset
 * ipc_sigrecvd to zero. */
/* TODO: Another probably more robust solution is to allow only a single
 * thread to admit/evict memory simultaneously. */
/* TODO: The same problem exists for the sigon/sigoff stuff, what if two
 * threads call sigon, then one thread calls sigoff. */
/* TODO: A possible solution would be to keep a counter of the number of
 * threads in sigon state, but then how to make sure that the signal gets
 * handled by **a** thread correctly. Does it matter which thread receives
 * signal? */
/****************************************************************************/
__thread volatile sig_atomic_t ipc_sigrecvd;


/****************************************************************************/
/*! Constructs which implement a critical section. */
/****************************************************************************/
#define CRITICAL_SECTION_BEG(__IPC)\
do {\
  int ret;\
  (__IPC)->flags[(__IPC)->id] |= IPC_MEM_BLOCKED;\
  ret = sem_wait((__IPC)->mtx);\
  (__IPC)->flags[(__IPC)->id] &= ~IPC_MEM_BLOCKED;\
  if (-1 == ret && (EINTR != errno || 0 == ipc_sigrecvd))\
    goto CRITICAL_SECTION_ERREXIT;\
  if (1 == ipc_sigrecvd) {\
    if (-1 != ret) {\
      ret = sem_post((__IPC)->mtx);\
      if (-1 == ret)\
        goto CRITICAL_SECTION_CLEANUP1;\
    }\
    goto CRITICAL_SECTION_ERRAGAIN;\
  }\
} while (0)

#define CRITICAL_SECTION_END(__IPC)\
do {\
  int ret;\
  ret = sem_post((__IPC)->mtx);\
  if (-1 == ret)\
    goto CRITICAL_SECTION_CLEANUP1;\
  goto CRITICAL_SECTION_DONE;\
  CRITICAL_SECTION_CLEANUP1:\
  ret = sem_post((__IPC)->mtx);\
  ASSERT(-1 != ret);\
  CRITICAL_SECTION_ERREXIT:\
  return -1;\
  CRITICAL_SECTION_ERRAGAIN:\
  return -2;\
  CRITICAL_SECTION_DONE:\
  (void)0;\
} while (0)


/****************************************************************************/
/*! Cause process to wait for some event to occur. */
/****************************************************************************/
SBMA_STATIC int
__ipc_wait(struct ipc * const __ipc)
{

  int ret;
  struct timespec ts;
#if SBMA_WAIT_ALGO == 1
  int state, sval1, sval2;
#endif

#if SBMA_WAIT_ALGO == 0
  {
    /* This approach is solely used as an early alert method. This means that
     * instead of requiring the process to sleep for the whole period, it can
     * be signalled early if another process enters the CMD_BLOCKED state. */
    ret = clock_gettime(CLOCK_REALTIME, &ts);
    if (-1 == ret)
      return -1;
    if ((long)999999999-ts.tv_nsec < (long)100000000) {
      ts.tv_sec++;
      ts.tv_nsec = (long)100000000-((long)999999999-ts.tv_nsec);
    }
    else {
      ts.tv_nsec += 100000000;
    }

    __ipc->flags[__ipc->id] |= IPC_MEM_BLOCKED;
    ret = sem_timedwait(__ipc->trn3, &ts);
    __ipc->flags[__ipc->id] &= ~IPC_MEM_BLOCKED;
    if (-1 == ret)
      return -1;
  }
#elif SBMA_WAIT_ALGO == 1
  {
    state      = 0;
    ts.tv_sec  = 0;
    ts.tv_nsec = 50000000;

    __ipc->flags[__ipc->id] |= IPC_MEM_BLOCKED;
    state |= 2;
    ret = sem_post(__ipc->cnt);
    if (-1 == ret)
      goto CLEANUP1;
    state |= 4;

    for (;;) {
      ret = libc_sem_wait(__ipc->trn2);
      if (-1 == ret)
        goto CLEANUP1;
      state |= 1;

      ret = sem_getvalue(__ipc->trn3, &sval1);
      if (-1 == ret)
        goto CLEANUP1;
      ret = sem_getvalue(__ipc->cnt, &sval2);
      if (-1 == ret)
        goto CLEANUP1;

      if (1 == sval1 || __ipc->n_procs == sval2) {
        if (1 == sval1) {
          ret = libc_sem_wait(__ipc->trn3);
          if (-1 == ret)
            goto CLEANUP1;
        }

        ret = sem_post(__ipc->trn2);
        if (-1 == ret)
          goto CLEANUP1;
        state &= ~1;

        break;
      }

      ret = sem_post(__ipc->trn2);
      if (-1 == ret)
        goto CLEANUP1;
      state &= ~1;

      ret = libc_nanosleep(&ts, NULL);
      if (-1 == ret)
        goto CLEANUP1;
    }

    __ipc->flags[__ipc->id] &= ~IPC_MEM_BLOCKED;
    state &= ~2;
    ret = libc_sem_wait(__ipc->cnt);
    if (-1 == ret)
      goto CLEANUP1;
    state &= ~4;

    return 0;

    CLEANUP1:
    if (0 != state) {
      if (1 == (state&1)) {
        ret = sem_post(__ipc->trn2);
        ASSERT(-1 != ret);
      }
      if (2 == (state&2)) {
        __ipc->flags[__ipc->id] &= ~IPC_MEM_BLOCKED;
      }
      /*if (4 == (state&4)) {
        ret = libc_sem_wait(__ipc->cnt);
        ASSERT(-1 != ret);
      }*/
    }
    return -1;
  }
#elif SBMA_WAIT_ALGO == 2
  {
    ts.tv_sec  = 0;
    ts.tv_nsec = 1000000;
    __ipc->flags[__ipc->id] |= IPC_MEM_BLOCKED;
    ret = nanosleep(&ts, NULL);
    __ipc->flags[__ipc->id] &= ~IPC_MEM_BLOCKED;
    if (-1 == ret)
      return -1;
  }
#endif

  return 0;
}


SBMA_EXTERN int
__ipc_init(struct ipc * const __ipc, int const __uniq, int const __n_procs,
           size_t const __max_mem)
{
  int ret, shm_fd, id;
  void * shm;
  sem_t * mtx, * trn1, * trn2, * trn3, * cnt, * sid;
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
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-cnt-%d", __uniq))
    return -1;
  cnt = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 0);
  if (SEM_FAILED == cnt)
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
  __ipc->cnt      = cnt;
  __ipc->smem     = (size_t*)shm;
  __ipc->c_mem    = (size_t*)((uintptr_t)__ipc->smem+sizeof(size_t));
  __ipc->pid      = (int*)((uintptr_t)__ipc->c_mem+(__n_procs*sizeof(size_t)));
  __ipc->flags    = (uint8_t*)((uintptr_t)idp+sizeof(int));

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

  __ipc->curpages = __ipc->c_mem[__ipc->id];

  ret = munmap((void*)__ipc->shm, IPC_LEN(__ipc->n_procs));
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
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-cnt-%d", __ipc->uniq))
    return -1;
  ret = sem_close(__ipc->cnt);
  if (-1 == ret)
    return -1;
  ret = sem_unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;

  return 0;
}
SBMA_EXPORT(internal, int
__ipc_destroy(struct ipc * const __ipc));


SBMA_EXTERN void
__ipc_populate(struct ipc * const __ipc)
{
  if (1 == __ipc->init)
    __ipc->flags[__ipc->id] |= IPC_POPULATED;
}
SBMA_EXPORT(internal, void
__ipc_populate(struct ipc * const __ipc));


SBMA_EXTERN void
__ipc_unpopulate(struct ipc * const __ipc)
{
  if (1 == __ipc->init)
    __ipc->flags[__ipc->id] &= ~IPC_POPULATED;
}
SBMA_EXPORT(internal, void
__ipc_unpopulate(struct ipc * const __ipc));


SBMA_EXTERN void
__ipc_sigon(struct ipc * const __ipc)
{
  ipc_sigrecvd = 0;
  if (1 == __ipc->init)
    __ipc->flags[__ipc->id] |= IPC_SIGON;
#if SBMA_WAIT_ALGO == 1
  if (1 == __ipc->init) {
    int ret = sem_post(__ipc->cnt);
    ASSERT(-1 != ret);
  }
#endif
}
SBMA_EXPORT(internal, void
__ipc_sigon(struct ipc * const __ipc));


SBMA_EXTERN void
__ipc_sigoff(struct ipc * const __ipc)
{
  if (1 == __ipc->init)
    __ipc->flags[__ipc->id] &= ~IPC_SIGON;
#if SBMA_WAIT_ALGO == 1
  if (1 == __ipc->init) {
    int ret = libc_sem_wait(__ipc->cnt);
    ASSERT(-1 != ret);
  }
#endif
}
SBMA_EXPORT(internal, void
__ipc_sigoff(struct ipc * const __ipc));


SBMA_EXTERN int
__ipc_eligible(struct ipc * const __ipc)
{
  if (1 == __ipc->init)
    return (IPC_ELIGIBLE == (__ipc->flags[__ipc->id]&IPC_ELIGIBLE));
  return 0;
}
SBMA_EXPORT(internal, int
__ipc_eligible(struct ipc * const __ipc));


SBMA_EXTERN int
__ipc_release(struct ipc * const __ipc)
{
#if SBMA_WAIT_ALGO == 0 || SBMA_WAIT_ALGO == 1
  int ret, sval;

  if (0 == __ipc->init)
    return 0;

  ret = libc_sem_wait(__ipc->trn2);
  if (-1 == ret)
    goto ERREXIT;

  ret = sem_getvalue(__ipc->trn3, &sval);
  if (-1 == ret)
    goto CLEANUP1;

  if (0 == sval) {
    ret = sem_post(__ipc->trn3);
    if (-1 == ret)
      goto CLEANUP1;
  }

  ret = sem_post(__ipc->trn2);
  if (-1 == ret)
    goto CLEANUP1;

  return 0;

  CLEANUP1:
  ret = sem_post(__ipc->trn2);
  ASSERT(-1 != ret);
  ERREXIT:
  return -1;
#else
  if (NULL == __ipc) {} /* to surpress unused warning */
  return 0;
#endif
}
SBMA_EXPORT(internal, int
__ipc_release(struct ipc * const __ipc));


SBMA_EXTERN void
__ipc_atomic_inc(struct ipc * const __ipc, size_t const __value)
{
  ASSERT(*__ipc->smem >= __value);

  *__ipc->smem -= __value;
  __ipc->c_mem[__ipc->id] += __value;

  if (__ipc->c_mem[__ipc->id] > __ipc->maxpages)
    __ipc->maxpages = __ipc->c_mem[__ipc->id];
}
SBMA_EXPORT(internal, void
__ipc_atomic_inc(struct ipc * const __ipc, size_t const __value));


SBMA_EXTERN void
__ipc_atomic_dec(struct ipc * const __ipc, size_t const __value)
{
  ASSERT(__ipc->c_mem[__ipc->id] >= __value);

  *__ipc->smem += __value;
  __ipc->c_mem[__ipc->id] -= __value;
}
SBMA_EXPORT(internal, void
__ipc_atomic_dec(struct ipc * const __ipc, size_t const __value));


SBMA_EXTERN int
__ipc_madmit(struct ipc * const __ipc, size_t const __value)
{
  int ret, i, ii, id;
  size_t mxmem, smem;
  int * pid;
  volatile uint8_t * flags;
  volatile size_t * c_mem;

  if (0 == __value)
    return 0;

  id    = __ipc->id;
  c_mem = __ipc->c_mem;
  pid   = __ipc->pid;
  flags = __ipc->flags;

  /*========================================================================*/
  CRITICAL_SECTION_BEG(__ipc);
  /*========================================================================*/

  smem = *__ipc->smem;
  while (smem < __value) {
    ii = -1;
    mxmem = 0;
    /* find a candidate process to release memory */
    for (i=0; i<__ipc->n_procs; ++i) {
      /* skip oneself */
      if (i == id)
        continue;

      /* skip process which are ineligible */
      if ((IPC_ELIGIBLE != (flags[i]&IPC_ELIGIBLE)) ||\
          (IPC_MEM_BLOCKED == (flags[i]&IPC_MEM_BLOCKED) && c_mem[id]<c_mem[i]))
      {
        continue;
      }

      /*
       *  Choose the process to evict as follows:
       *    1) If no candidate process has resident memory greater than the
       *       requested memory, then choose the candidate which has the most
       *       resident memory.
       *    2) If some candidate process(es) have resident memory greater than
       *       the requested memory, then choose from these, the candidate
       *       which has the least.
       */
      if (((mxmem < __value-smem && c_mem[i] > mxmem) ||\
          (c_mem[i] >= __value-smem && c_mem[i] < mxmem)))
      {
        ii = i;
        mxmem = c_mem[i];
      }
    }

    /* no valid candidate process exists, break loop */
    if (-1 == ii)
      break;

    /* tell the chosen candidate process to free memory */
    ret = kill(pid[ii], SIGIPC);
    if (-1 == ret)
      goto CLEANUP1;

    /* wait for it to signal it has finished */
    ret = libc_sem_wait(__ipc->trn1);
    if (-1 == ret)
      goto CLEANUP1;

    smem = *__ipc->smem;
  }

  if (smem >= __value) {
    __ipc_atomic_inc(__ipc, __value);
    __ipc_populate(__ipc);
  }

  /*========================================================================*/
  CRITICAL_SECTION_END(__ipc);
  /*========================================================================*/

#if SBMA_VERSION >= 200
  if (smem < __value) {
    ret = __ipc_wait(__ipc);
    if (-1 == ret && EINTR != errno && ETIMEDOUT != errno)
      goto ERREXIT;

    goto ERRAGAIN;
  }
#endif

  ASSERT(smem >= __value);
  return 0;

  CLEANUP1:
  ret = sem_post(__ipc->mtx);
  ASSERT(-1 != ret);
  ERREXIT:
  return -1;
  ERRAGAIN:
  return -2;
}
SBMA_EXPORT(internal, int
__ipc_madmit(struct ipc * const __ipc, size_t const __value));


SBMA_EXTERN int
__ipc_mevict(struct ipc * const __ipc, size_t const __value)
{
  if (0 == __value)
    return 0;

  /*========================================================================*/
  CRITICAL_SECTION_BEG(__ipc);
  /*========================================================================*/

  __ipc_atomic_dec(__ipc, __value);

  /*========================================================================*/
  CRITICAL_SECTION_END(__ipc);
  /*========================================================================*/

  return 0;
}
SBMA_EXPORT(internal, int
__ipc_mevict(struct ipc * const __ipc, size_t const __value));
