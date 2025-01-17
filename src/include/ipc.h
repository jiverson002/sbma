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


#ifndef SBMA_IPC_H
#define SBMA_IPC_H 1


#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif


#include <pthread.h>   /* pthread library */
#include <semaphore.h> /* semaphore library */
#include <stdint.h>    /* uint8_t */
#include <stddef.h>    /* size_t */
#include "common.h"
#include "sbma.h"


/* TODO Change int to pid_t for storing process IDs */


/*****************************************************************************/
/*  Signal used to tell other processes to release memory. */
/*****************************************************************************/
#define SIGIPC (SIGRTMIN+0)


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
  int _ret;\
  _ret = sem_wait((IPC)->inter_mtx);\
  ASSERT(0 == _ret);\
} while (0)

#define IPC_INTER_CRITICAL_SECTION_END(IPC)\
do {\
  int _ret;\
  _ret = sem_post((IPC)->inter_mtx);\
  ASSERT(0 == _ret);\
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
/*
 *  Inter-process communication process status bits:
 *
 *    bit 0 ==    0: signals blocked  1: signals unblocked
 */
/*****************************************************************************/
enum ipc_code
{
  IPC_SIGON = 1 << 0
};


/*****************************************************************************/
/*  Interprocess environment. */
/*****************************************************************************/
struct ipc
{
  int init;        /*!< initialized indicator */

  int id;          /*!< ipc id of process amoung the n_procs */
  int n_procs;     /*!< number of processes in coordination */

  int uniq;        /*!< unique identifier for shared mem and semaphores */

  size_t curpages; /*!< current pages loaded */
  size_t maxpages; /*!< maximum number of pages loaded */

  sem_t * inter_mtx;         /*!< inter-process critical section mutex */
  sem_t * done;              /*!< indicator that signal handler is completed */
  sem_t * sid;               /*!< unique id among processes within a node */
  sem_t * sig;               /*!< counter of threads with signaling enabled */
  pthread_mutex_t intra_mtx; /*!< intra-process critical section mutex */

  void * shm;               /*!< shared memory region */
  int * pid;                /*!< pointer into shm for pid array */
  volatile size_t  * s_mem; /*!< pointer into shm for system mem scalar */
  volatile size_t  * c_mem; /*!< pointer into shm for current resident mem array */
  volatile size_t  * d_mem; /*!< pointer into shm for dirty mem array */
  volatile uint8_t * flags; /*!< pointer into shm for flags array */
};


#ifdef __cplusplus
extern "C" {
#endif


/*****************************************************************************/
/*  Initialize the interprocess environment. */
/*****************************************************************************/
SBMA_EXPORT(internal, int
ipc_init(struct ipc * const ipc, int const uniq, int const n_procs,
         size_t const max_mem));


/*****************************************************************************/
/*  Destroy the interprocess environment. */
/*****************************************************************************/
SBMA_EXPORT(internal, int
ipc_destroy(struct ipc * const ipc));


/*****************************************************************************/
/*  Allow signals to be sent to process. */
/*****************************************************************************/
SBMA_EXPORT(internal, void
ipc_sigon(struct ipc * const ipc));


/*****************************************************************************/
/*  Disallow signals to be sent to process. */
/*****************************************************************************/
SBMA_EXPORT(internal, void
ipc_sigoff(struct ipc * const ipc));


/*****************************************************************************/
/*  Check if process is eligible for eviction. */
/*****************************************************************************/
SBMA_EXPORT(internal, int
ipc_is_eligible(struct ipc * const ipc, int const id));


/*****************************************************************************/
/*  Increment process resident memory. */
/*****************************************************************************/
SBMA_EXPORT(internal, void
ipc_atomic_inc(struct ipc * const ipc, size_t const value));


/*****************************************************************************/
/*  Decrement process resident memory. */
/*****************************************************************************/
SBMA_EXPORT(internal, void
ipc_atomic_dec(struct ipc * const ipc, size_t const c_pages,
               size_t const d_pages));


/*****************************************************************************/
/*  Account for resident memory before admission. Check to see if the system
 *  can support the addition of value bytes of memory. */
/*****************************************************************************/
SBMA_EXPORT(internal, int
ipc_madmit(struct ipc * const ipc, size_t const value, int const admitd));


/*****************************************************************************/
/*  Account for loaded memory after eviction. */
/*****************************************************************************/
SBMA_EXPORT(internal, int
ipc_mevict(struct ipc * const ipc, size_t const c_pages,
           size_t const d_pages));


/*****************************************************************************/
/*  Account for dirty memory. */
/*****************************************************************************/
SBMA_EXPORT(internal, int
ipc_mdirty(struct ipc * const ipc, ssize_t const value));


#ifdef __cplusplus
}
#endif


#endif /* SBMA_IPC_H */
