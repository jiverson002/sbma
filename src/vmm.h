#ifndef __VMM_H__
#define __VMM_H__ 1

#include <signal.h> /* signal library */
#include <stddef.h> /* size_t */

#include "mmu.h"


/****************************************************************************/
/* Option constants */
/****************************************************************************/
enum vmm_opts
{
  VMM_LAZYREAD = 1 << 0 /* directive to enable lazy reading */
};


/****************************************************************************/
/* Stores information associated with the virtual memory environment. */
/****************************************************************************/
struct vmm
{
  int opts;                 /*!< environment configuation */

  size_t page_size;         /*!< bytes per sbmalloc page */

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

  int (*acct_charge_cb)(size_t);    /*!< function pointers for accounting */
  int (*acct_discharge_cb)(size_t); /*!< ... */

  struct mmu mmu;           /*!< memory managment unit */

#ifdef USE_PTHREAD
  pthread_mutex_t lock;     /*!< mutex guarding struct */
#endif
};


static struct mmu mmu;
static struct vmm vmm;


/****************************************************************************/
/* Increments a particular field in the info struct. */
/****************************************************************************/
#define __vmm_track__(__VMM, __FIELD, __VAL)\
do {\
  if (0 == LOCK_GET(&((__VMM).lock))) {\
    (__VMM).__FIELD += (__VAL);\
    (void)LOCK_LET(&((__VMM).lock));\
  }\
} while (0)


/****************************************************************************/
/*! Swaps the supplied range of pages in, reading any necessary pages from
 *  disk. */
/****************************************************************************/
static inline ssize_t
__vmm_admit__(struct vmm * const __vmm, void * const __addr,
              size_t const __num, int const __prot, int const __admit)
{
  int ret;

  /* __addr must be multiples of page_size */
  if (0 != ((uintptr_t)__addr & (__vmm->page_size-1)))
    return -1;

  /* update protection of pages */
  ret = __mmu_protect__(&(__vmm->mmu), __addr, __num, __prot, __admit);
  if (-1 == ret)
    return -1;

  return 0;
}


/****************************************************************************/
/*! Swaps the supplied range of pages out, writing any dirty pages to
 *  disk. */
/****************************************************************************/
static inline ssize_t
__vmm_evict__(struct vmm * const __vmm, void * const __addr,
              size_t const __num)
{
  int ret;

  /* __addr must be multiples of page_size */
  if (0 != ((uintptr_t)__addr & (__vmm->page_size-1)))
    return -1;

  /* update protection of pages */
  ret = __mmu_protect__(&(__vmm->mmu), __addr, __num, PROT_NONE, MMU_FLUSH);
  if (-1 == ret)
    return -1;

  return 0;
}


#if 0
/****************************************************************************/
/*! Clear the MMU_DIRTY and reset MMU_ZFILL flags for the supplied range of
 *  pages. */
/****************************************************************************/
static inline ssize_t
__vmm_clear__(struct vmm * const __vmm, void * const __addr,
              size_t const __num)
{
  int ret;
  size_t i, beg, end, page_size;
  ssize_t ii;
  struct ate * ate;
  struct mmu * mmu;

  mmu       = &(__vmm->mmu);
  page_size = mmu->page_size;

  /* __addr must be multiples of page_size */
  assert(0 == ((uintptr_t)__addr & (page_size-1)));

  /* get allocation table entry */
  ate = __mmu_get_ate__(mmu, __addr);

  /* __addr and __num must be valid w.r.t. ate */
  assert(NULL != ate);
  assert(ate->base <= __addr);
  assert(__num*page_size <= ate->len);
  assert(__addr <= (void*)((uintptr_t)ate->base+ate->len-__num*page_size));

  /* get range of pages */
  beg = __mmu_get_idx__(mmu, __addr);
  end = beg+__num;

  /* loop through range, clearing resetting memory protection in bulk whenever
   * possible */
  for (ii=-1,i=beg; i<=end; ++i) {
    if (i != end && !__mmu_get_flag__(mmu, i, MMU_DIRTY)) {
      if (-1 == ii)
        ii = i;
    }
    else if (-1 != ii) {
      ret = mprotect((void*)((uintptr_t)__addr+((ii-beg)*page_size)),\
        (i-ii)*page_size, PROT_READ);
      if (-1 == ret)
        return -1;

      ii = -1;
    }

    if (i != end) {
      assert(__mmu_get_flag__(mmu, i, MMU_ALLOC));

      __mmu_unset_flag__(mmu, i, MMU_DIRTY);
      __mmu_set_flag__(mmu, i, MMU_ZFILL);
    }
  }

  return 0;
}
#endif


/****************************************************************************/
/*! The SIGSEGV handler. */
/****************************************************************************/
static inline void
__vmm_sigsegv__(int const sig, siginfo_t * const si, void * const ctx)
{
  int ret;
  size_t idx, l_pages;
  ssize_t numrd;
  void * addr;
  struct ate * ate;

  /* make sure we received a SIGSEGV */
  assert(SIGSEGV == sig);

  addr = (void*)((uintptr_t)si->si_addr & (mmu.page_size-1));
  idx  = __mmu_get_idx__(&mmu, addr);

  /* acquire lock on alloction */
  ate = __mmu_lock_ate__(&mmu, addr);

  if (__mmu_get_flag__(&mmu, idx, MMU_RSDNT)) {
    /* sanity check */
    assert(!__mmu_get_flag__(&mmu, idx, MMU_DIRTY));

    /* update protection to read-write */
    ret = __vmm_admit__(&vmm, addr, 1, PROT_READ|PROT_WRITE, MMU_NOADMIT);
    assert(-1 != ret);

    /* release lock on alloction */
    __mmu_unlock_ate__(&mmu, ate);

    /* track number of write faults */
    __vmm_track__(vmm, numwf, 1);
  }
  else {
    /* invoke charge callback function before swapping in any memory -- this
     * is only done once for each allocation between successive swap outs */
    if (0 == ate->l_pages && NULL != vmm.acct_charge_cb)
      (void)(*vmm.acct_charge_cb)(__mmu_to_sys__(&mmu, ate->n_pages));

    /* swap in the required memory */
    if (VMM_LAZYREAD == (vmm.opts & VMM_LAZYREAD)) {
      assert(0 == ate->l_pages);
      l_pages = ate->n_pages;
      ret     = __vmm_admit__(&vmm, ate->base, ate->n_pages, PROT_READ,\
        MMU_ADMIT);
      assert(-1 != ret);
    }
    else {
      l_pages = 1;
      ret     = __vmm_admit__(&vmm, addr, 1, PROT_READ, MMU_ADMIT);
      assert(-1 != numrd);
    }

    /* release lock on alloction */
    __mmu_unlock_ate__(&mmu, ate);

    /* track number of read faults, syspages read from disk, syspages
     * currently loaded, and high water mark for syspages loaded */
    __vmm_track__(vmm, numrf, 1);
    __vmm_track__(vmm, numrd, numrd);
    __vmm_track__(vmm, curpages, __mmu_to_sys__(&mmu, l_pages));
    __vmm_track__(vmm, maxpages,\
      vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);
  }

  if (NULL == ctx) {} /* suppress unused warning */
}


/****************************************************************************/
/*! Initializes the virtual memory manager. */
/****************************************************************************/
static inline int
__vmm_init__(struct vmm * const __vmm, size_t const __page_size,
             size_t const __min_alloc_size, int const __opts)
{
  int ret;

  __vmm->opts      = __opts;
  __vmm->page_size = __page_size;

  /* setup the signal handler */
  __vmm->act.sa_flags     = SA_SIGINFO;
  __vmm->act.sa_sigaction = __vmm_sigsegv__;
  ret = sigemptyset(&(__vmm->act.sa_mask));
  if (-1 == ret)
    return -1;
  ret = sigaction(SIGSEGV, &(__vmm->act), &(__vmm->oldact));
  if (-1 == ret)
    return -1;

  ret = __mmu_init__(&(__vmm->mmu), __page_size, __min_alloc_size);
  if (-1 == ret)
    return -1;

  return 0;

#if 0
  struct rlimit lim;

  /* make sure that rlimit for memlock is as large as allowed */
  ret = getrlimit(RLIMIT_MEMLOCK, &lim);
  assert(-1 != ret);
  lim.rlim_cur = lim.rlim_max;
  /*printf("[%5d] %zu %zu\n", (int)getpid(), lim.rlim_cur, lim.rlim_max);*/
  ret = setrlimit(RLIMIT_MEMLOCK, &lim);
  assert(-1 != ret);
#endif
}


/****************************************************************************/
/*! Shuts down the virtual memory manager. */
/****************************************************************************/
static inline int
__vmm_destroy__(struct vmm * const __vmm)
{
  int ret;

  ret = sigaction(SIGSEGV, &(__vmm->oldact), NULL);
  if (-1 == ret)
    return -1;

  ret = __mmu_destroy__(&(__vmm->mmu));
  if (-1 == ret)
    return -1;

  return 0;
}


#endif
