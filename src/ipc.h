#ifndef __IPC_H__
#define __IPC_H__ 1


#include <assert.h>    /* assert library */
#include <errno.h>     /* errno */
#include <fcntl.h>     /* O_RDWR, O_CREAT, O_EXCL */
#include <semaphore.h> /* semaphore library */
#include <signal.h>    /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t */
#include <stdio.h>     /* FILENAME_MAX */
#include <string.h>    /* memcpy */
#include <sys/mman.h>  /* mmap, mremap, munmap, madvise, mprotect */
#include <sys/stat.h>  /* S_IRUSR, S_IWUSR */
#include <sys/types.h> /* ftruncate */
#include <unistd.h>    /* ftruncate */
#include "config.h"
#include "sbma.h"


#define IPC_SHM  "/shm-bdmpi-sbma-ipc"
#define IPC_MTX  "/sem-bdmpi-sbma-ipc-mtx"
#define IPC_CNT  "/sem-bdmpi-sbma-ipc-cnt"
#define IPC_TRN1 "/sem-bdmpi-sbma-ipc-trn1"
#define IPC_TRN2 "/sem-bdmpi-sbma-ipc-trn2"

#define SIGIPC   (SIGRTMIN+0)


#define IPC_BARRIER(__IPC)\
do {\
  int __ipc_ret, __ipc_i, __ipc_count;\
  /* mutex.wait() */\
  __ipc_ret = sem_wait((__IPC)->mtx);\
  assert(-1 != __ipc_ret);\
  /* count += 1 */\
  __ipc_ret = sem_post((__IPC)->cnt);\
  assert(-1 != __ipc_ret);\
  /* if count == n: */\
  __ipc_ret = sem_getvalue((__IPC)->cnt, &__ipc_count);\
  assert(-1 != __ipc_ret);\
  if((__IPC)->n_procs == __ipc_count) {\
    /* turnstile.signal(n) # unlock the first */\
    for(__ipc_i=0; __ipc_i<(__IPC)->n_procs; ++__ipc_i) {\
      __ipc_ret = sem_post((__IPC)->trn1);\
      assert(-1 != __ipc_ret);\
    }\
  }\
  /* mutex.signal() */\
  __ipc_ret = sem_post((__IPC)->mtx);\
  assert(-1 != __ipc_ret);\
  /* turnstile.wait() # first turnstile */\
  __ipc_ret = sem_wait((__IPC)->trn1);\
  assert(-1 != __ipc_ret);\
\
  /* mutex.wait() */\
  __ipc_ret = sem_wait((__IPC)->mtx);\
  assert(-1 != __ipc_ret);\
  /* count -= 1 */\
  __ipc_ret = sem_wait((__IPC)->cnt);\
  assert(-1 != __ipc_ret);\
  /* if count == 0: */\
  __ipc_ret = sem_getvalue((__IPC)->cnt, &__ipc_count);\
  assert(-1 != __ipc_ret);\
  if(0 == __ipc_count) {\
    /* turnstile2.signal(n) # unlock the second */\
    for(__ipc_i=0; __ipc_i<(__IPC)->n_procs; ++__ipc_i) {\
      __ipc_ret = sem_post((__IPC)->trn2);\
      assert(-1 != __ipc_ret);\
    }\
  }\
  /* mutex.signal() */\
  __ipc_ret = sem_post((__IPC)->mtx);\
  assert(-1 != __ipc_ret);\
  /* turnstile2.wait() # second turnstile */\
  __ipc_ret = sem_wait((__IPC)->trn2);\
  assert(-1 != __ipc_ret);\
} while (0)


#define IPC_LEN(__N_PROCS)\
  (sizeof(ssize_t)+(__N_PROCS)*(sizeof(size_t)+sizeof(int)+sizeof(uint8_t))+\
    sizeof(int))


/****************************************************************************/
/*! Interprocess environment. */
/****************************************************************************/
struct ipc
{
  int id;        /*!< ipc id of process amoung the n_procs */
  int n_procs;   /*!< number of processes in coordination */

  sem_t * mtx;   /*!< critical section semaphores */
  sem_t * cnt;   /*!< ... */
  sem_t * trn1;  /*!< ... */
  sem_t * trn2;  /*!< ... */

  void * shm;     /*!< shared memory region */
  ssize_t * smem; /*!< pointer into shm for smem scalar */
  size_t * pmem;  /*!< pointer into shm for pmem array */
  int * pid;      /*!< pointer into shm for pid array */

  uint8_t * flags; /*!< pointer into shm for flags array */
};


/****************************************************************************/
/*! Initialize the interprocess environment. */
/****************************************************************************/
static int
__ipc_init__(struct ipc * const __ipc, int const __n_procs,
             size_t const __max_mem)
{
  int ret, shm_fd, id;
  void * shm;
  sem_t * mtx, * cnt, * trn1, * trn2;
  int * idp;

  /* initialize semaphores */
  mtx = sem_open(IPC_MTX, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 1);
  if (SEM_FAILED == mtx)
    return -1;
  cnt = sem_open(IPC_CNT, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 0);
  if (SEM_FAILED == cnt)
    return -1;
  trn1 = sem_open(IPC_TRN1, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 0);
  if (SEM_FAILED == trn1)
    return -1;
  trn2 = sem_open(IPC_TRN2, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, 0);
  if (SEM_FAILED == trn2)
    return -1;

  /* try to create a new shared memory region -- if i create, then i should
   * also truncate it, if i dont create, then try and just open it. */
  shm_fd = shm_open(IPC_SHM, O_RDWR|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);
  if (-1 == shm_fd) {
    if (EEXIST == errno) {
      shm_fd = shm_open(IPC_SHM, O_RDWR, S_IRUSR|S_IWUSR);
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

  /* id pointer is last sizeof(int) bytes of shm */
  idp = (int*)((uintptr_t)shm+IPC_LEN(__n_procs)-sizeof(int));
  id  = (*idp)++;

  if (id >= __n_procs)
    return -1;

  /* setup ipc struct */
  __ipc->id      = id;
  __ipc->n_procs = __n_procs;
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

  //IPC_BARRIER(__ipc);

  return 0;
}


/****************************************************************************/
/*! Destroy the interprocess environment. */
/****************************************************************************/
static int
__ipc_destroy__(struct ipc * const __ipc)
{
  int ret;

  ret = munmap(__ipc->shm, IPC_LEN(__ipc->n_procs));
  if (-1 == ret)
    return -1;

  ret = shm_unlink(IPC_SHM);
  if (-1 == ret && ENOENT != errno)
    return -1;

  ret = sem_close(__ipc->mtx);
  if (-1 == ret)
    return -1;
  ret = sem_unlink(IPC_MTX);
  if (-1 == ret && ENOENT != errno)
    return -1;

  ret = sem_close(__ipc->cnt);
  if (-1 == ret)
    return -1;
  ret = sem_unlink(IPC_CNT);
  if (-1 == ret && ENOENT != errno)
    return -1;

  ret = sem_close(__ipc->trn1);
  if (-1 == ret)
    return -1;
  ret = sem_unlink(IPC_TRN1);
  if (-1 == ret && ENOENT != errno)
    return -1;

  ret = sem_close(__ipc->trn2);
  if (-1 == ret)
    return -1;
  ret = sem_unlink(IPC_TRN2);
  if (-1 == ret && ENOENT != errno)
    return -1;

  return 0;
}


/****************************************************************************/
/*! Change elibibility for eviction for the process. */
/****************************************************************************/
static int
__ipc_eligible__(struct ipc * const __ipc, int const __eligible)
{
  int ret;

  ret = sem_wait(__ipc->mtx);
  if (-1 == ret)
    return -1;

  if (IPC_ELIGIBLE == (__eligible&IPC_ELIGIBLE))
    __ipc->flags[__ipc->id] |= IPC_ELIGIBLE;
  else
    __ipc->flags[__ipc->id] &= ~IPC_ELIGIBLE;

  ret = sem_post(__ipc->mtx);
  if (-1 == ret)
    return -1;

  return 0;
}


/****************************************************************************/
/*! Account for resident memory before admission. Check to see if the system
 *  can support the addition of __value bytes of memory. */
/****************************************************************************/
static ssize_t
__ipc_madmit__(struct ipc * const __ipc, size_t const __value)
{
  int ret, i, ii;
  ssize_t smem, mxmem;
  uint8_t * flags;
  int * pid;
  size_t * pmem;

  ret = sem_wait(__ipc->mtx);
  if (-1 == ret)
    return -1;

  // 1) check to see if there is enough free system memory
  // 2) if not, then signal to the process with the most memory
  //   2.1) __ipc_init__ should install a signal handler on the said signal
  //        which will call __vmm_mevictall__
  //   2.2) this will require BDMPL_SLEEP to check the return value of
  //        bdmp_recv for EINTR
  // 3) repeat until enough free memory or no processes have any loaded memory

  /* update system and process memory counts */
  *__ipc->smem -= __value;
  __ipc->pmem[__ipc->id] += __value;

  smem  = *__ipc->smem;
  pmem  = __ipc->pmem;
  pid   = __ipc->pid;
  flags = __ipc->flags;

  while (smem < 0) {
    /* find a process which has memory and is eligible */
    mxmem = 0;
    ii    = -1;
    for (i=0; i<__ipc->n_procs; ++i) {
      if (i == __ipc->id)
        continue;

      if (pmem[i] > mxmem && IPC_ELIGIBLE == (flags[i]&IPC_ELIGIBLE)) {
        ii = i;
        mxmem = pmem[i];
      }
    }

    /* no such process is exists, break loop */
    if (-1 == ii)
      break;

    /*printf("%5d ==SIGIPC==> %5d (%zu)\n", (int)getpid(), pid[ii], pmem[ii]);*/

    /* such a process is available, tell it to free memory */
    ret = kill(pid[ii], SIGIPC);
    if (-1 == ret) {
      (void)sem_post(__ipc->mtx);
      return -1;
    }

    /* wait for it to signal it has finished */
    ret = sem_wait(__ipc->trn1);
    if (-1 == ret) {
      (void)sem_post(__ipc->mtx);
      return -1;
    }

    smem  = *__ipc->smem;
  }

  ret = sem_post(__ipc->mtx);
  if (-1 == ret)
    return -1;

  return smem;
}


/****************************************************************************/
/*! Account for loaded memory after eviction. */
/****************************************************************************/
static int
__ipc_mevict__(struct ipc * const __ipc, ssize_t const __value)
{
  int ret;

  if (__value > 0)
    return -1;

  ret = sem_wait(__ipc->mtx);
  if (-1 == ret)
    return -1;

  *__ipc->smem -= __value;
  assert(__ipc->pmem[__ipc->id] >= -__value);
  __ipc->pmem[__ipc->id] += __value;

  ret = sem_post(__ipc->mtx);
  if (-1 == ret)
    return -1;

  return 0;
}


#endif
