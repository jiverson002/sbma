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
#include <unistd.h>    /* ftruncate */
#include "config.h"
#include "ipc.h"
#include "sbma.h"


#define IPC_LEN(__N_PROCS)\
  (sizeof(ssize_t)+(__N_PROCS)*(sizeof(size_t)+sizeof(int)+sizeof(uint8_t))+\
    sizeof(int))


/* Thread static variables for checking to see if a SIGIPC was received and
 * honored bewteen __ipc_block() and __ipc_unblock(). */
static __thread size_t _ipc_l_pages;
static __thread int    _ipc_sigrecvd;


SBMA_EXTERN int
__ipc_init(struct ipc * const __ipc, int const __uniq, int const __n_procs,
           size_t const __max_mem)
{
  int ret, shm_fd, id;
  void * shm;
  sem_t * mtx, * cnt, * trn1, * trn2, * sid;
  int * idp;
  char fname[FILENAME_MAX];

  /* initialize semaphores */
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-mtx-%d", __uniq))
    return -1;
  mtx = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 1);
  if (SEM_FAILED == mtx)
    return -1;
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-cnt-%d", __uniq))
    return -1;
  cnt = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 0);
  if (SEM_FAILED == cnt)
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
  idp = (int*)((uintptr_t)shm+IPC_LEN(__n_procs)-sizeof(int));
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
  __ipc->init    = 1;
  __ipc->id      = id;
  __ipc->n_procs = __n_procs;
  __ipc->uniq    = __uniq;
  __ipc->shm     = shm;
  __ipc->mtx     = mtx;
  __ipc->cnt     = cnt;
  __ipc->trn1    = trn1;
  __ipc->trn2    = trn2;
  __ipc->smem    = (ssize_t*)shm;
  __ipc->pmem    = (size_t*)((uintptr_t)__ipc->smem+sizeof(ssize_t));
  __ipc->pid     = (int*)((uintptr_t)__ipc->pmem+(__n_procs*sizeof(size_t)));
  __ipc->flags   = (uint8_t*)((uintptr_t)__ipc->pid+(__n_procs*sizeof(int)));

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

  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-cnt-%d", __ipc->uniq))
    return -1;
  ret = sem_close(__ipc->cnt);
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

  return 0;
}
SBMA_EXPORT(internal, int
__ipc_destroy(struct ipc * const __ipc));


SBMA_EXTERN int
__ipc_block(struct ipc * const __ipc)
{
  if (0 == __ipc->init)
    return 0;

  /* Get number of loaded pages when process is in a state where reception of
   * SIGIPC is not honored. */
  _ipc_l_pages  = __ipc->pmem[__ipc->id];
  _ipc_sigrecvd = 0;

  /* Transition to blocked */
  __ipc->flags[__ipc->id] |= IPC_BLOCKED;

  return 0;
}
SBMA_EXPORT(internal, int
__ipc_block(struct ipc * const __ipc));


SBMA_EXTERN int
__ipc_unblock(struct ipc * const __ipc)
{
  if (0 == __ipc->init)
    return 0;

  /* Transition to running */
  __ipc->flags[__ipc->id] &= ~IPC_BLOCKED;

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
  return _ipc_sigrecvd;
}
SBMA_EXPORT(internal, int
__ipc_sigrecvd(struct ipc * const __ipc));


SBMA_EXTERN int
__ipc_is_eligible(struct ipc * const __ipc)
{
  uint8_t const flag = (IPC_BLOCKED|IPC_POPULATED);
  if (0 == __ipc->init)
    return 0;
  return (flag == (__ipc->flags[__ipc->id]&flag));
}
SBMA_EXPORT(internal, int
__ipc_is_eligible(struct ipc * const __ipc));


SBMA_EXTERN ssize_t
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

  int ret, i, ii, dlctr;
  size_t mxmem;
  ssize_t smem;
  struct timespec ts;
  uint8_t * flags;
  int * pid;
  size_t * pmem;

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

  smem  = *__ipc->smem-__value;
  pmem  = __ipc->pmem;
  pid   = __ipc->pid;
  flags = __ipc->flags;

  while (smem < 0) {
    /* find a process which has memory and is eligible */
    mxmem = 0;
    ii    = -1;
    for (dlctr=0,i=0; i<__ipc->n_procs; ++i) {
      dlctr += (IPC_BLOCKED == (flags[i]&(IPC_BLOCKED|IPC_POPULATED)));

      if (i == __ipc->id)
        continue;

      if ((IPC_BLOCKED|IPC_POPULATED) == (flags[i]&(IPC_BLOCKED|IPC_POPULATED))) {
        if (pmem[i] > mxmem) {
        /* choose this process if it has more resident memory than the
         * candidate process AND (i am running OR my resident memory is larger
         * than theirs). */
        /*if (pmem[i] > mxmem &&
            (IPC_BLOCKED != (flags[__ipc->id]&IPC_BLOCKED) ||
             pmem[__ipc->id] >= pmem[i]))
        {*/
          ii = i;
          mxmem = pmem[i];
        }
      }
    }

    /* no such process exists, break loop */
    if (-1 == ii) {
      ASSERT(dlctr != __ipc->n_procs);
      break;
    }

    /* such a process is available, tell it to free memory */
    ret = kill(pid[ii], SIGIPC);
    if (-1 == ret) {
      (void)sem_post(__ipc->mtx);
      return -1;
    }

    /* wait for it to signal it has finished */
    ret = libc_sem_wait(__ipc->trn1);
    if (-1 == ret) {
      ASSERT(EINTR != errno);
      (void)sem_post(__ipc->mtx);
      return -1;
    }

    smem  = *__ipc->smem-__value;
  }

  if (smem >= 0) {
    *__ipc->smem -= __value;
    __ipc->pmem[__ipc->id] += __value;
  }

  ret = sem_post(__ipc->mtx);
  if (-1 == ret)
    return -1;

  if (smem < 0) {
    ts.tv_sec  = 0;
    ts.tv_nsec = 250000000;
    ret = nanosleep(&ts, NULL);
    if (-1 == ret && EINTR != errno)
      return -1;

    ASSERT(0 == __ipc_is_eligible(__ipc));
    errno = EAGAIN;
    return -1;
  }

  ASSERT(smem >= 0);

  ret = __ipc_populate(__ipc);
  if (-1 == ret)
    return -1;

  ASSERT(0 == __ipc_is_eligible(__ipc));
  return smem;
}
SBMA_EXPORT(internal, ssize_t
__ipc_madmit(struct ipc * const __ipc, size_t const __value));


SBMA_EXTERN int
__ipc_mevict(struct ipc * const __ipc, ssize_t const __value)
{
  int ret;

  if (__value > 0)
    return -1;

  ASSERT(0 == __ipc_is_eligible(__ipc));

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

  if (__ipc->pmem[__ipc->id] < (size_t)(-__value))
    printf("[%5d] %s:%d %zu,%zd\n", (int)getpid(), __func__, __LINE__,
      __ipc->pmem[__ipc->id], __value);
  ASSERT(__ipc->pmem[__ipc->id] >= (size_t)(-__value));

  *__ipc->smem -= __value;
  __ipc->pmem[__ipc->id] += __value;

  ret = sem_post(__ipc->mtx);
  if (-1 == ret)
    return -1;

  ASSERT(0 == __ipc_is_eligible(__ipc));

  return 0;
}
SBMA_EXPORT(internal, int
__ipc_mevict(struct ipc * const __ipc, ssize_t const __value));
