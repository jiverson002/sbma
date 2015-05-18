#ifndef __VMM_H__
#define __VMM_H__ 1

#include <signal.h> /* signal library */
#include <stddef.h> /* size_t */

#include "mmu.h"


/****************************************************************************/
/* Stores information associated with the virtual memory environment. */
/****************************************************************************/
struct vmm
{
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
/*! Swaps the supplied range of pages in, reading any necessary pages from
 *  disk. */
/****************************************************************************/
static inline ssize_t
__vmm_admit__(struct vmm * const __vmm, void * const __addr,
              size_t const __num)
{
  int fd, ret;
  size_t i, beg, end, numrd, page_size;
  ssize_t ii;
  void * ghost;
  struct ate * ate;
  struct mmu * mmu;

  numrd     = 0;
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

  /* allocate ghost pages */
  ghost = (void*)mmap(NULL, __num*page_size, PROT_WRITE, MMAP_FLAGS, -1, 0);
  if (MAP_FAILED == ghost)
    return -1;

#if 0
  /* open the file for reading */
  fd = libc_open(ate->file, O_RDONLY);
  if (-1 == fd)
    return -1;
#endif

  /* loop through range, reading from disk in bulk whenever possible */
  for (ii=-1,i=beg; i<=end; ++i) {
    if (i != end) {
      assert(__mmu_get_flag__(mmu, i, MMU_ALLOC));
      assert(!__mmu_get_flag__(mmu, i, MMU_DIRTY));

      __mmu_set_flag__(mmu, i, MMU_RSDNT);
    }

    if (i != end && !__mmu_get_flag__(mmu, i, MMU_ZFILL)) {
      if (-1 == ii)
        ii = i;
    }
    else if (-1 != ii) {
#if 0
      /* read from disk */
      ret = __mmu_read__(fd, (void*)(ghost+((ii-beg)*page_size)),\
        (i-ii)*page_size, ii*page_size);
      if (-1 == ret)
        return -1;
#endif

      numrd += (i-ii);

      ii = -1;
    }
  }

#if 0
  /* close file */
  ret = close(fd);
  if (-1 == ret)
    return -1;
#endif

  /* give rd protection of ghost pages */
  ret = mprotect(ghost, __num*page_size, PROT_READ);
  if (-1 == ret)
    return -1;
  /* remap ghost pages into persistent memory */
  ghost = mremap(ghost, __num*page_size, __num*page_size,\
    MREMAP_MAYMOVE|MREMAP_FIXED, __addr);
  if (MAP_FAILED == ghost)
    return -1;

  ate->l_pages += __num;

  return numrd;
}


/****************************************************************************/
/*! Swaps the supplied range of pages out, writing any dirty pages to
 *  disk. */
/****************************************************************************/
static inline ssize_t
__vmm_evict__(struct vmm * const __vmm, void * const __addr,
              size_t const __num)
{
  int fd, ret;
  size_t i, beg, end, numwr, page_size;
  ssize_t ii;
  struct ate * ate;
  struct mmu * mmu;

  numwr     = 0;
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

  /* shortcut by checking to see if no pages are currently loaded */
  /* TODO: if we track the number of dirty pages, then this can do a better
   * job of short-cutting */
  if (0 == ate->l_pages)
    return 0;

  /* get range of pages */
  beg = __mmu_get_idx__(mmu, __addr);
  end = beg+__num;

#if 0
  /* open the file for reading */
  fd = libc_open(ate->file, O_WRONLY);
  if (-1 == fd)
    return -1;
#endif

  /* loop through range, writing to disk in bulk whenever possible */
  for (ii=-1,i=beg; i<=end; ++i) {
    if (i != end && __mmu_get_flag__(mmu, i, MMU_RSDNT)) {
      assert(__mmu_get_flag__(mmu, i, MMU_ALLOC));

      assert(ate->l_pages > 0);
      ate->l_pages--;

      __mmu_unset_flag__(mmu, i, MMU_RSDNT);
    }

    if (i != end && __mmu_get_flag__(mmu, i, MMU_DIRTY)) {
      assert(__mmu_get_flag__(mmu, i, MMU_ZFILL));

      if (-1 == ii)
        ii = i;

      __mmu_unset_flag__(mmu, i, MMU_ZFILL);
    }
    else if (-1 != ii) {
#if 0
      /* write to disk */
      ret = __mmu_write__(fd, (void*)(__addr+((ii-beg)*page_size)),\
        (i-ii)*page_size, ii*page_size);
      if (-1 == ret)
        return -1;
#endif

      numwr += (i-ii);

      ii = -1;
    }
  }

#if 0
  /* close file */
  ret = close(fd);
  if (-1 == ret)
    return -1;
#endif

  /* unlock pages from RAM */
  ret = munlock(__addr, __num*page_size);
  if (-1 == ret)
    return -1;
  /* give no protection to page */
  ret = mprotect(__addr, __num*page_size, PROT_NONE);
  if (-1 == ret)
    return -1;
  /* advise kernel to release resources associated with pages */
  ret = madvise(__addr, __num*page_size, MADV_DONTNEED);
  if (-1 == ret)
    return -1;

  return numwr;
}


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


/****************************************************************************/
/*! The SIGSEGV handler. */
/****************************************************************************/
static inline void
__vmm_sigsegv__(int const sig, siginfo_t * const si, void * const ctx)
{
  size_t idx, page_size, l_pages;
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

    /* update page flag */
    __mmu_set_flag__(&mmu, idx, MMU_DIRTY|MMU_ZFILL);

    /* release lock on alloction */
    __mmu_unlock_ate__(&mmu, ate);

#if 0
    /* track number of write faults */
    __INFO_TRACK__(sb_info.numwf, 1);
#endif
  }
  else {
#if 0
    /* invoke charge callback function before swapping in any memory -- this
     * is only done once for each allocation between successive swap outs */
    if (0 == ate->l_pages && NULL != vmm.acct_charge_cb)
      (void)(*vmm.acct_charge_cb)(SB_TO_SYS(ate->n_pages, page_size));
#endif

    /* swap in the required memory */
#if 0
    if (0 == sb_opts[SBOPT_LAZYREAD]) {
#endif
      l_pages = ate->n_pages-ate->l_pages;
      numrd   = __vmm_admit__(&vmm, ate->base, ate->n_pages);
      assert(-1 != numrd);
#if 0
    }
    else {
      l_pages = 1;
      numrd   = __vmm_admit__(&vmm, addr, 1);
      assert(-1 != numrd);
    }
#endif

    /* release lock on alloction */
    __mmu_unlock_ate__(&mmu, ate);

#if 0
    /* track number of read faults, syspages read from disk, syspages
     * currently loaded, and high water mark for syspages loaded */
    __INFO_TRACK__(sb_info.numrf, 1);
    __INFO_TRACK__(sb_info.numrd, numrd);
    __INFO_TRACK__(sb_info.curpages, SB_TO_SYS(ld_pages, psize));
    __INFO_TRACK__(sb_info.maxpages,
      sb_info.curpages>sb_info.maxpages?sb_info.curpages-sb_info.maxpages:0);
#endif
  }

  if (NULL == ctx) {} /* suppress unused warning */
}


/****************************************************************************/
/*! Initializes the virtual memory manager. */
/****************************************************************************/
static inline int
__vmm_init__(struct vmm * const __vmm, size_t const __page_size,
             size_t const __min_alloc_size)
{
  int ret;

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
