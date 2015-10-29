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

#ifndef __IPC_H__
#define __IPC_H__ 1


#include <pthread.h>   /* pthread library */
#include <semaphore.h> /* semaphore library */
#include <signal.h>    /* sig_atomic_t */
#include <stdint.h>    /* uint8_t */
#include <stddef.h>    /* size_t */


#define SIGIPC (SIGRTMIN+0)


/****************************************************************************/
/*! Compute the length of the IPC shared memory segment. */
/****************************************************************************/
#define IPC_LEN(__N_PROCS)\
  (sizeof(size_t)+(__N_PROCS)*(sizeof(int)+sizeof(size_t)+sizeof(size_t)+\
    sizeof(uint8_t))+sizeof(int))


/****************************************************************************/
/*!
 * Inter-process communication process status bits:
 *
 *   bit 0 ==    0: unpopulated      1: populated
 *   bit 1 ==    0: signals blocked  1: signals unblocked
 *   bit 2 ==    0:                  1: blocked in madmit
 */
/****************************************************************************/
enum ipc_code
{
  IPC_POPULATED    = 1 << 0,
  IPC_SIGON        = 1 << 1,
  IPC_MEM_BLOCKED  = 1 << 2,
  IPC_ELIGIBLE     = IPC_POPULATED|IPC_SIGON
};


/****************************************************************************/
/*! Interprocess environment. */
/****************************************************************************/
struct ipc
{
  int init;         /*!< initialized indicator */

  int id;           /*!< ipc id of process amoung the n_procs */
  int n_procs;      /*!< number of processes in coordination */

  int uniq;         /*!< unique identifier for shared mem and semaphores */

  size_t curpages;  /*!< current pages loaded */
  size_t maxpages;  /*!< maximum number of pages loaded */

  sem_t * mtx;      /*!< critical section semaphores */
  sem_t * trn1;     /*!< ... */
  sem_t * trn2;     /*!< ... */
  sem_t * trn3;     /*!< ... */
  sem_t * cnt;      /*!< ... */

  pthread_mutex_t thread_mutex; /*!< thread mutex */

  void * shm;               /*!< shared memory region */
  int * pid;                /*!< pointer into shm for pid array */
  volatile size_t  * smem;  /*!< pointer into shm for smem scalar */
  volatile size_t  * c_mem; /*!< pointer into shm for c_mem array */
  volatile size_t  * d_mem; /*!< pointer into shm for d_mem array */
  volatile uint8_t * flags; /*!< pointer into shm for flags array */
};


/****************************************************************************/
/*! Thread-local variable to check for signal received. */
/****************************************************************************/
extern __thread volatile sig_atomic_t ipc_sigrecvd;


#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************/
/*! Initialize the interprocess environment. */
/****************************************************************************/
int
__ipc_init(struct ipc * const __ipc, int const __uniq, int const __n_procs,
           size_t const __max_mem);


/****************************************************************************/
/*! Destroy the interprocess environment. */
/****************************************************************************/
int
__ipc_destroy(struct ipc * const __ipc);


/****************************************************************************/
/*! Transition process to populated status. */
/****************************************************************************/
void
__ipc_populate(struct ipc * const __ipc);


/****************************************************************************/
/*! Transition process to unpopulated status. */
/****************************************************************************/
void
__ipc_unpopulate(struct ipc * const __ipc);


/****************************************************************************/
/*! Allow signals to be sent to process. */
/****************************************************************************/
void
__ipc_sigon(struct ipc * const __ipc);


/****************************************************************************/
/*! Disallow signals to be sent to process. */
/****************************************************************************/
void
__ipc_sigoff(struct ipc * const __ipc);


/****************************************************************************/
/*! Check if process is eligible for eviction. */
/****************************************************************************/
int
__ipc_eligible(struct ipc * const __ipc);


/****************************************************************************/
/*! Release a memory blocked process, if any. */
/****************************************************************************/
int
__ipc_release(struct ipc * const __ipc);


/****************************************************************************/
/*! Increment process resident memory. */
/****************************************************************************/
void
__ipc_atomic_inc(struct ipc * const __ipc, size_t const __value);


/****************************************************************************/
/*! Decrement process resident memory. */
/****************************************************************************/
void
__ipc_atomic_dec(struct ipc * const __ipc, size_t const __c_pages,
                 size_t const __d_pages);


/****************************************************************************/
/*! Account for resident memory before admission. Check to see if the system
 *  can support the addition of __value bytes of memory. */
/****************************************************************************/
int
__ipc_madmit(struct ipc * const __ipc, size_t const __value,
             int const __admitd);


/****************************************************************************/
/*! Account for loaded memory after eviction. */
/****************************************************************************/
int
__ipc_mevict(struct ipc * const __ipc, size_t const __c_pages,
             size_t const __d_pages);


/****************************************************************************/
/*! Account for dirty memory. */
/****************************************************************************/
int
__ipc_mdirty(struct ipc * const __ipc, ssize_t const __value);

#ifdef __cplusplus
}
#endif


#endif
