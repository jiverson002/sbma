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

#ifndef __VMM_H__
#define __VMM_H__ 1


#include <signal.h>    /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stddef.h>    /* size_t */
#include <stdio.h>     /* FILENAME_MAX */
#include <sys/types.h> /* ssize_t */
#include "ipc.h"
#include "mmu.h"


/****************************************************************************/
/*! Virtual memory manager. */
/****************************************************************************/
struct vmm
{
  int init;                     /*!< initialized indicator */
  int opts;                     /*!< runtime options */

  size_t page_size;             /*!< bytes per page */

  volatile size_t numipc;       /*!< total number of SIGIPC received */
  volatile size_t numhipc;      /*!< total number of SIGIPC honored */

  volatile size_t numrf;        /*!< total number of read segfaults */
  volatile size_t numwf;        /*!< total number of write segfaults */
  volatile size_t numrd;        /*!< total number of pages read */
  volatile size_t numwr;        /*!< total number of pages written */
  volatile double tmrrd;        /*!< read timer */
  volatile double tmrwr;        /*!< write timer */

  size_t numpages;              /*!< current pages allocated */

  char fstem[FILENAME_MAX];     /*!< the file stem where the data is stored */

  struct sigaction act_segv;    /*!< for the SIGSEGV signal handler */
  struct sigaction oldact_segv; /*!< ... */
  struct sigaction act_ipc;     /*!< for the SIGIPC signal handler */
  struct sigaction oldact_ipc;  /*!< ... */

  struct mmu mmu;               /*!< memory management unit */
  struct ipc ipc;               /*!< interprocess communicator */

#ifdef USE_THREAD
  pthread_mutex_t lock;         /*!< mutex guarding struct */
#endif
};


/****************************************************************************/
/*! One instance of vmm per process. */
/****************************************************************************/
extern struct vmm vmm;


/****************************************************************************/
/*! Converts pages to system pages. */
/****************************************************************************/
#define VMM_TO_SYS(__N_PAGES)\
  ((__N_PAGES)*vmm.page_size/sysconf(_SC_PAGESIZE))


/****************************************************************************/
/*! Increments a particular field in the info struct. */
/****************************************************************************/
#define VMM_TRACK(__FIELD, __VAL)\
do {\
  if (0 != (__VAL) && 0 == __lock_get(&(vmm.lock))) {\
    vmm.__FIELD += (__VAL);\
    (void)__lock_let(&(vmm.lock));\
  }\
} while (0)


#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************/
/*! Swaps the supplied range of pages in, reading any necessary pages from
 *  disk. */
/****************************************************************************/
ssize_t
__vmm_swap_i(struct ate * const __ate, size_t const __beg,
             size_t const __num, int const __ghost);


/****************************************************************************/
/*! Swaps the supplied range of pages out, writing any dirty pages to
 *  disk. */
/****************************************************************************/
ssize_t
__vmm_swap_o(struct ate * const __ate, size_t const __beg,
             size_t const __num);


/****************************************************************************/
/*! Clear the MMU_DIRTY and set the MMU_ZFILL flags for the supplied range of
 *  pages */
/****************************************************************************/
ssize_t
__vmm_swap_x(struct ate * const __ate, size_t const __beg,
             size_t const __num);


/****************************************************************************/
/*! Initializes the sbmalloc subsystem. */
/****************************************************************************/
int
__vmm_init(struct vmm * const __vmm, char const * const __fstem,
           int const __uniq, size_t const __page_size, int const __n_procs,
           size_t const __max_mem, int const __opts);


/****************************************************************************/
/*! Shuts down the sbmalloc subsystem. */
/****************************************************************************/
int
__vmm_destroy(struct vmm * const __vmm);

#ifdef __cplusplus
}
#endif


#endif
