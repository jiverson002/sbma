#ifndef __VMM_H__
#define __VMM_H__ 1


#include <assert.h>      /* assert */
#include <errno.h>        /* errno */
#include <fcntl.h>        /* O_RDWR, O_CREAT, O_EXCL, open, posix_fadvise */
#include <malloc.h>       /* struct mallinfo */
#include <signal.h>       /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stdint.h>       /* uintptr_t */
#include <stdio.h>        /* stderr, fprintf */
#include <stdlib.h>       /* NULL */
#include <string.h>       /* memset */
#include <sys/mman.h>     /* mmap, munmap, madvise, mprotect */
#include <sys/resource.h> /* rlimit */
#include <sys/stat.h>     /* S_IRUSR, S_IWUSR, open */
#include <sys/time.h>     /* rlimit */
#include <sys/types.h>    /* open, ssize_t */
#include <unistd.h>       /* close, read, write, sysconf */

#include "config.h"
#include "mmu.h"


/****************************************************************************/
/* Virtual memory manager. */
/****************************************************************************/
struct vmm
{
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


#if 0
/****************************************************************************/
/*! The SIGSEGV handler. */
/****************************************************************************/
static inline void
__info_segv__(int const sig, siginfo_t * const si, void * const ctx)
{
  size_t ip, psize, ld_pages, numrd;
  uintptr_t addr;
  char * flags;
  struct sb_alloc * alloc;

  /* make sure we received a SIGSEGV */
  assert(SIGSEGV == sig);

  /* setup local variables */
  psize = sb_info.pagesize;
  addr  = (uintptr_t)si->si_addr;
  alloc = __info_find__(addr);
  assert(NULL != alloc); /* probably shouldn't just abort here, since this
                            will only succeed when a `bad' SIGSEGV has been
                            raised */

  SB_GET_LOCK(&(alloc->lock));
  ip    = (addr-alloc->base)/psize;
  flags = alloc->flags;

  if (__if_flags__(flags[ip], 0, SBPAGE_SYNC)) {
    /* invoke charge callback function before swapping in any memory -- this
     * is only done once for each allocation between successive swap outs */
    if (0 == alloc->ld_pages && NULL != sb_info.acct_charge_cb)
      (void)(*sb_info.acct_charge_cb)(SB_TO_SYS(alloc->n_pages, psize));

    /* swap in the required memory */
    if (0 == sb_opts[SBOPT_LAZYREAD]) {
      ld_pages = alloc->n_pages-alloc->ld_pages;
      numrd    = __swap_in__(alloc, 0, alloc->n_pages);
      assert(-1 != numrd);
    }
    else {
      ld_pages = 1;
      numrd    = __swap_in__(alloc, ip, 1);
      assert(-1 != numrd);
    }

    /* release lock on alloction */
    SB_LET_LOCK(&(alloc->lock));

    /* track number of read faults, syspages read from disk, syspages
     * currently loaded, and high water mark for syspages loaded */
    __INFO_TRACK__(sb_info.numrf, 1);
    __INFO_TRACK__(sb_info.numrd, numrd);
    __INFO_TRACK__(sb_info.curpages, SB_TO_SYS(ld_pages, psize));
    __INFO_TRACK__(sb_info.maxpages,
      sb_info.curpages>sb_info.maxpages?sb_info.curpages-sb_info.maxpages:0);
  }
  else {
    /* sanity check */
    assert(__if_flags__(flags[ip], SBPAGE_SYNC, SBPAGE_DIRTY));

    /* - all flags */
    /* + DIRTY flag */
    flags[ip] = SBPAGE_DIRTY;

    /* update protection to read-write */
    SBMPROTECT(alloc->base+(ip*psize), psize, PROT_READ|PROT_WRITE);

    /* release lock on alloction */
    SB_LET_LOCK(&(alloc->lock));

    /* track number of write faults */
    __INFO_TRACK__(sb_info.numwf, 1);
  }

  if (NULL == ctx) {} /* suppress unused warning */
}
#endif


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
             char const * const __fstem)
{
  /* set page size */
  __vmm->page_size = __page_size;

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

#if 0
  /* setup the signal handler */
  __vmm->act.sa_flags     = SA_SIGINFO;
  __vmm->act.sa_sigaction = __vmm_sigsegv__;
  if (-1 == sigemptyset(&(__vmm->act.sa_mask)))
    return -1;
  if (-1 == sigaction(SIGSEGV, &(__vmm->act), &(__vmm->oldact)))
    return -1;
#endif

  /* initialize mmu */
  if (-1 == __mmu_init__(&(__vmm->mmu), __page_size))
    return -1;

  /* initialize vmm lock */
  if (-1 == LOCK_INIT(&(__vmm->lock)))
    return -1;
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
}


#endif
