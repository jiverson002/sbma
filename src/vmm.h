#ifndef __VMM_H__
#define __VMM_H__ 1


#include <assert.h>   /* assert */
#include <signal.h>   /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stddef.h>   /* NULL, size_t */
#include <stdint.h>   /* uint8_t, uintptr_t */
#include <sys/mman.h> /* mmap, munmap, madvise, mprotect */
#include "config.h"
#include "mmu.h"


/****************************************************************************/
/* Virtual memory manager. */
/****************************************************************************/
struct vmm
{
  int opts;                 /*!< runtime options */

  size_t page_size;         /*!< bytes per page */

  size_t numrf;             /*!< total number of read segfaults */
  size_t numwf;             /*!< total number of write segfaults */
  size_t numrd;             /*!< total number of pages read */
  size_t numwr;             /*!< total number of pages written */
  size_t curpages;          /*!< current pages loaded */
  size_t maxpages;          /*!< maximum number of pages allocated */
  size_t numpages;          /*!< current pages allocated */

  char fstem[FILENAME_MAX]; /*!< the file stem where the data is stored */

  struct sigaction act;     /*!< for the SIGSEGV signal handler */
  struct sigaction oldact;  /*!< ... */

  struct mmu mmu;           /*!< memory management unit */

#ifdef USE_PTHREAD
  pthread_mutex_t lock;     /*!< mutex guarding struct */
#endif
};


/****************************************************************************/
/*! The SIGSEGV handler. */
/****************************************************************************/
static inline void
__vmm_sigsegv__(int const sig, siginfo_t * const si, void * const ctx)
{
#if 0
  size_t ip, psize, ld_pages, numrd;
  uintptr_t addr;
  char * flags;
  struct sb_alloc * alloc;

  /* make sure we received a SIGSEGV */
  assert(SIGSEGV == sig);

  /* setup local variables */
  page_size = sb_info.pagesize;
  addr      = (uintptr_t)si->si_addr;

  /* locate allocation table entry */
  ate = __mmu_locate__(addr);
  assert(NULL != ate);

  ip    = (addr-ate->base)/page_size;
  flags = ate->flags;

  if (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) {
    /* TODO: check memory file to see if there is enough free memory to complete
     * this allocation. */

    /* swap in the required memory */
    if (VMM_LZYRD == (vmm.opts&VMM_LZYRD)) {
      l_pages = 1;
      numrd   = __vmm_swap_o__(ate, ip, 1);
      assert(-1 != numrd);
    }
    else {
      l_pages = ate->n_pages-ate->l_pages;
      numrd   = __vmm_swap_i__(ate, 0, ate->n_pages);
      assert(-1 != numrd);
    }

    /* release lock on alloction table entry */
    LOCK_LET(&(ate->lock));

    /* track number of read faults, syspages read from disk, syspages
     * currently loaded, and high water mark for syspages loaded */
    __vmm_track__(numrf, 1);
    __vmm_track__(numrd, numrd);
    __vmm_track__(curpages, __vmm_to_sys__(l_pages));
    __vmm_track__(maxpages,
      vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);
  }
  else {
    /* sanity check */
    assert(MMU_RSDNT == (flags[ip]&MMU_RSDNT));
    assert(MMU_DIRTY != (flags[ip]&MMU_DIRTY));

    /* give dirty and resident flags */
    flags[ip] = MMU_DIRTY|MMU_RSDNT;

    /* update protection to read-write */
    ret = mprotect((void*)(ate->base+(ip*page_size)), page_size,\
      PROT_READ|PROT_WRITE);
    assert(-1 != ret);

    /* release lock on alloction table entry */
    LOCK_LET(&(ate->lock));

    /* track number of write faults */
    __vmm_track__(numwf, 1);
  }

  if (NULL == ctx) {} /* suppress unused warning */
#endif
  if (0 == sig || NULL == si || NULL == ctx) {}
}


/****************************************************************************/
/* Increments a particular field in the info struct. */
/****************************************************************************/
#define __vmm_track__(__FIELD, __VAL)\
do {\
  if (0 == LOCK_GET(&(vmm.lock))) {\
    vmm.__FIELD += (__VAL);\
    (void)LOCK_LET(&(vmm.lock));\
  }\
} while (0)


/****************************************************************************/
/*! Initializes the sbmalloc subsystem. */
/****************************************************************************/
static inline int
__vmm_init__(struct vmm * const __vmm, size_t const __page_size,
             char const * const __fstem, int const __opts)
{
  /* set page size */
  __vmm->page_size = __page_size;

  /* set options */
  __vmm->opts = __opts;

  /* initialize statistics */
  __vmm->numrf    = 0;
  __vmm->numwf    = 0;
  __vmm->numrd    = 0;
  __vmm->numwr    = 0;
  __vmm->curpages = 0;
  __vmm->maxpages = 0;
  __vmm->numpages = 0;

  /* copy file stem */
  strncpy(__vmm->fstem, __fstem, FILENAME_MAX-1);
  __vmm->fstem[FILENAME_MAX-1] = '\0';

  /* setup the signal handler */
  __vmm->act.sa_flags     = SA_SIGINFO;
  __vmm->act.sa_sigaction = __vmm_sigsegv__;
  if (-1 == sigemptyset(&(__vmm->act.sa_mask)))
    return -1;
  if (-1 == sigaction(SIGSEGV, &(__vmm->act), &(__vmm->oldact)))
    return -1;

  /* initialize mmu */
  if (-1 == __mmu_init__(&(__vmm->mmu), __page_size))
    return -1;

  /* initialize vmm lock */
  if (-1 == LOCK_INIT(&(__vmm->lock)))
    return -1;

  return 0;
}


/****************************************************************************/
/*! Shuts down the sbmalloc subsystem. */
/****************************************************************************/
static inline int
__vmm_destroy__(struct vmm * const __vmm)
{
  /* reset signal handler */
  if (-1 == sigaction(SIGSEGV, &(__vmm->oldact), NULL))
    return -1;

  /* destroy mmu lock */
  if (-1 == LOCK_FREE(&(__vmm->lock)))
    return -1;

  return 0;
}


#endif
