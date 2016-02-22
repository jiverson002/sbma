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


#ifndef SBMA_VMM_H
#define SBMA_VMM_H 1


#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif


#include <signal.h>    /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stddef.h>    /* size_t */
#include <stdio.h>     /* FILENAME_MAX */
#include <sys/types.h> /* ssize_t */
#include "ipc.h"
#include "mmu.h"


/*****************************************************************************/
/*  Virtual memory manager. */
/*****************************************************************************/
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


/*****************************************************************************/
/*  One instance of vmm per process. */
/*****************************************************************************/
extern struct vmm _vmm_;


/*****************************************************************************/
/*  Converts pages to system pages. */
/*****************************************************************************/
#define VMM_TO_SYS(N_PAGES)\
  ((size_t)(N_PAGES)*_vmm_.page_size/(size_t)sysconf(_SC_PAGESIZE))


/*****************************************************************************/
/*  Constructs which implement a intra-process critical section. */
/*****************************************************************************/
#define VMM_INTRA_CRITICAL_SECTION_BEG(VMM)\
do {\
  int _ret;\
  _ret = lock_get(&((VMM)->lock));\
  ASSERT(0 == _ret);\
} while (0)

#define VMM_INTRA_CRITICAL_SECTION_END(VMM)\
do {\
  int _ret;\
  _ret = lock_let(&((VMM)->lock));\
  ASSERT(0 == _ret);\
} while (0)


/*****************************************************************************/
/*  Increments a particular field in the info struct. */
/*****************************************************************************/
#define VMM_TRACK(VMM, FIELD, VAL)\
do {\
  (VMM)->FIELD += (VAL);\
} while (0)


#ifdef __cplusplus
extern "C" {
#endif


/*****************************************************************************/
/*  Swaps the supplied range of pages in, reading any necessary pages from
 *  disk. */
/*****************************************************************************/
SBMA_EXPORT(internal, ssize_t
vmm_swap_i(struct ate * const ate, size_t const beg, size_t const num,
           int const ghost));


/*****************************************************************************/
/*  Swaps the supplied range of pages out, writing any dirty pages to
 *  disk. */
/*****************************************************************************/
SBMA_EXPORT(internal, ssize_t
vmm_swap_o(struct ate * const ate, size_t const beg, size_t const num));


/*****************************************************************************/
/*  Clear the MMU_DIRTY and set the MMU_ZFILL flags for the supplied range of
 *  pages */
/*****************************************************************************/
SBMA_EXPORT(internal, ssize_t
vmm_swap_x(struct ate * const ate, size_t const beg, size_t const num));


/*****************************************************************************/
/*  Initializes the sbmalloc subsystem. */
/*****************************************************************************/
SBMA_EXPORT(internal, int
vmm_init(struct vmm * const vmm, char const * const fstem, int const uniq,
         size_t const page_size, int const n_procs, size_t const max_mem,
         int const opts));


/*****************************************************************************/
/*  Shuts down the sbmalloc subsystem. */
/*****************************************************************************/
SBMA_EXPORT(internal, int
vmm_destroy(struct vmm * const vmm));


#ifdef __cplusplus
}
#endif


#endif /* SBMA_VMM_H */
