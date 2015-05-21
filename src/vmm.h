#ifndef __VMM_H__
#define __VMM_H__ 1


#include <assert.h>   /* assert */
#include <fcntl.h>    /* O_RDWR, O_CREAT, O_EXCL */
#include <signal.h>   /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stddef.h>   /* NULL, size_t */
#include <stdint.h>   /* uint8_t, uintptr_t */
#include <stdio.h>    /* FILENAME_MAX */
#include <string.h>   /* strncpy */
#include <sys/mman.h> /* mmap, mremap, madvise, mprotect */
#include <unistd.h>   /* sysconf */
#include "config.h"
#include "sbma.h"
#include "ipc.h"
#include "mmu.h"



/****************************************************************************/
/*! Virtual memory manager. */
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
/*! One instance of vmm per process. */
/****************************************************************************/
extern struct vmm vmm;


/****************************************************************************/
/*! Converts pages to system pages. */
/****************************************************************************/
#define __vmm_to_sys__(__N_PAGES)\
  ((__N_PAGES)*vmm.page_size/sysconf(_SC_PAGESIZE))


/****************************************************************************/
/*! Increments a particular field in the info struct. */
/****************************************************************************/
#define __vmm_track__(__FIELD, __VAL)\
do {\
  if (0 == LOCK_GET(&(vmm.lock))) {\
    vmm.__FIELD += (__VAL);\
    (void)LOCK_LET(&(vmm.lock));\
  }\
} while (0)


/****************************************************************************/
/*! Read data from file. */
/****************************************************************************/
static inline int
__vmm_read__(int const __fd, void * const __buf, size_t __len, size_t __off)
{
  ssize_t len;
  char * buf = (char*)__buf;

#ifndef HAVE_PREAD
  if (-1 == lseek(__fd, __off, SEEK_SET))
    return -1;
#endif

  do {
#ifdef HAVE_PREAD
    if (-1 == (len=libc_pread(__fd, buf, __len, __off)))
      return -1;
    __off += len;
#else
    if (-1 == (len=libc_read(__fd, buf, __len)))
      return -1;
#endif

    buf += len;
    __len -= len;
  } while (__len > 0);

  return 0;
}


/****************************************************************************/
/*! Write data to file. */
/****************************************************************************/
static inline int
__vmm_write__(int const __fd, void const * const __buf, size_t __len,
              size_t __off)
{
  ssize_t len;
  char * buf = (char*)__buf;

#ifndef HAVE_PWRITE
  if (-1 == lseek(__fd, __off, SEEK_SET))
    return -1;
#endif

  do {
#ifdef HAVE_PWRITE
    if (-1 == (len=libc_pwrite(__fd, buf, __len, __off)))
      return -1;
    __off += len;
#else
    if (-1 == (len=libc_write(__fd, buf, __len)))
      return -1;
#endif

    buf += len;
    __len -= len;
  } while (__len > 0);

  return 0;
}


/****************************************************************************/
/*! Swaps the supplied range of pages in, reading any necessary pages from
 *  disk. */
/****************************************************************************/
static inline ssize_t
__vmm_swap_i__(struct ate * const __ate, size_t const __beg,
               size_t const __num)
{
  int ret, fd;
  size_t ip, page_size, end, numrd=0;
  ssize_t ipfirst;
  uintptr_t addr;
  uint8_t * flags;
  char fname[FILENAME_MAX];

  /* error check input values */
  assert(NULL != __ate);
  assert(__num <= __ate->n_pages);
  assert(__beg <= __ate->n_pages-__num);

  /* shortcut */
  if (0 == __num)
    return 0;

  /* shortcut by checking to see if all pages are already loaded */
  if (__ate->l_pages == __ate->n_pages)
    return 0;

  /* setup local variables */
  page_size = vmm.page_size;
  flags     = __ate->flags;
  end       = __beg+__num;

  /* mmap temporary memory with write protection for loading from disk */
  addr = (uintptr_t)mmap(NULL, __num*page_size, PROT_WRITE,\
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_LOCKED, -1, 0);
  if ((uintptr_t)MAP_FAILED == addr)
    return -1;

  /* compute file name */
  if (0 > snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    (uintptr_t)__ate))
  {
    return -1;
  }
  /* open the file for reading */
  fd = libc_open(fname, O_RDONLY);
  if (-1 == fd)
    return -1;

  /* load only those pages which were previously written to disk and have
   * not since been dumped */
  for (ipfirst=-1,ip=__beg; ip<=end; ++ip) {
    if (ip != end &&\
        (MMU_DIRTY != (flags[ip]&MMU_DIRTY)) &&  /* not dirty */\
        (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) &&  /* not resident */\
        (MMU_ZFILL == (flags[ip]&MMU_ZFILL)))    /* cannot be zero filled */
    {
      if (-1 == ipfirst)
        ipfirst = ip;
    }
    else if (-1 != ipfirst) {
      ret = __vmm_read__(fd, (void*)(addr+((ipfirst-__beg)*page_size)),
        (ip-ipfirst)*page_size, ipfirst*page_size);
      if (-1 == ret)
        return -1;

      numrd += (ip-ipfirst);

      ipfirst = -1;
    }

    if (ip != end) {
      if (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) {
        assert(__ate->l_pages < __ate->n_pages-1);
        __ate->l_pages++;

        /* flag: *0* */
        flags[ip] &= ~MMU_RSDNT;
      }
      else {
        /* copy data from already resident pages */
        memcpy((void*)(addr+((ip-__beg)*page_size)),\
          (void*)(__ate->base+(ip*page_size)), page_size);
      }
    }
  }

  /* close file */
  ret = libc_close(fd);
  if (-1 == ret)
    return -1;

  /* update protection of temporary mapping to read-only */
  ret = mprotect((void*)addr, __num*page_size, PROT_READ);
  if (-1 == ret)
    return -1;

  /* update protection of temporary mapping and copy data for any dirty pages
   * */
  for (ip=__beg; ip<end; ++ip) {
    if (MMU_DIRTY == (flags[ip]&MMU_DIRTY)) {
      ret = mprotect((void*)(addr+((ip-__beg)*page_size)), page_size,\
        PROT_READ|PROT_WRITE);
      if (-1 == ret)
        return -1;
    }
  }

  /* remap temporary memory into the persistent memory */
  addr = (uintptr_t)mremap((void*)addr, __num*page_size, __num*page_size,\
    MREMAP_MAYMOVE|MREMAP_FIXED, __ate->base+(__beg*page_size));
  if ((uintptr_t)MAP_FAILED == addr)
    return -1;

  /* return the number of pages read from disk */
  return numrd;
}


/****************************************************************************/
/*! Swaps the supplied range of pages out, writing any dirty pages to
 *  disk. */
/****************************************************************************/
static inline ssize_t
__vmm_swap_o__(struct ate * const __ate, size_t const __beg,
               size_t const __num)
{
  int ret, fd;
  size_t ip, page_size, end, numwr=0;
  ssize_t ipfirst;
  uintptr_t addr;
  uint8_t * flags;
  char fname[FILENAME_MAX];

  /* error check input values */
  assert(NULL != __ate);
  assert(__num <= __ate->n_pages);
  assert(__beg <= __ate->n_pages-__num);

  /* shortcut */
  if (0 == __num)
    return 0;

  /* shortcut by checking to see if no pages are currently loaded */
  /* TODO: if we track the number of dirty pages, then this can do a better
   * job of short-cutting */
  if (0 == __ate->l_pages)
    return 0;

  /* setup local variables */
  page_size = vmm.page_size;
  addr      = __ate->base;
  flags     = __ate->flags;
  end       = __beg+__num;

  /* compute file name */
  if (0 > snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    (uintptr_t)__ate))
  {
    return -1;
  }
  /* open the file for writing */
  fd = libc_open(fname, O_WRONLY);
  if (-1 == fd)
    return -1;

  /* go over the pages and write the ones that have changed. perform the
   * writes in contigous chunks of changed pages. */
  for (ipfirst=-1,ip=__beg; ip<=end; ++ip) {
    if (ip != end && (MMU_DIRTY != (flags[ip]&MMU_DIRTY))) {
      if (MMU_RSDNT != (flags[ip]&MMU_RSDNT)) {
        assert(__ate->l_pages > 0);
        __ate->l_pages--;
      }

      /* flag: 01* */
      flags[ip] &= MMU_ZFILL;
      flags[ip] |= MMU_RSDNT;
    }

    if (ip != end && (MMU_DIRTY == (flags[ip]&MMU_DIRTY))) {
      assert(MMU_RSDNT != (flags[ip]&MMU_RSDNT)); /* is resident */

      if (-1 == ipfirst)
        ipfirst = ip;

      assert(__ate->l_pages > 0);
      __ate->l_pages--;

      /* flag: 011 */
      flags[ip] = MMU_RSDNT|MMU_ZFILL;
    }
    else if (-1 != ipfirst) {
      ret = __vmm_write__(fd, (void*)(addr+(ipfirst*page_size)),\
        (ip-ipfirst)*page_size, ipfirst*page_size);
      if (-1 == ret)
        return -1;

      numwr += (ip-ipfirst);

      ipfirst = -1;
    }
  }

  /* close file */
  ret = libc_close(fd);
  if (-1 == ret)
    return -1;

  /* unlock the memory from RAM */
  ret = libc_munlock((void*)(addr+(__beg*page_size)), __num*page_size);
  if (-1 == ret)
    return -1;

  /* update its protection to none */
  ret = mprotect((void*)(addr+(__beg*page_size)), __num*page_size, PROT_NONE);
  if (-1 == ret)
    return -1;

  /* unlock the memory, update its protection to none and advise kernel to
   * release its associated resources */
  ret = madvise((void*)(addr+(__beg*page_size)), __num*page_size,\
    MADV_DONTNEED);
  if (-1 == ret)
    return -1;

  /* return the number of pages written to disk */
  return numwr;
}


/****************************************************************************/
/*! Clear the MMU_DIRTY and set the MMU_ZFILL flags for the supplied range of
 *  pages */
/****************************************************************************/
static inline ssize_t
__vmm_swap_x__(struct ate * const __ate, size_t const __beg,
               size_t const __num)
{
  int ret;
  size_t ip, end, page_size;
  uint8_t * flags;

  /* error check input values */
  assert(NULL != __ate);
  assert(__num <= __ate->n_pages);
  assert(__beg <= __ate->n_pages-__num);

  /* shortcut */
  if (0 == __num)
    return 0;

  /* shortcut by checking to see if no pages are currently loaded */
  /* TODO: if we track the number of dirty pages, then this can do a better
   * job of short-cutting */
  if (0 == __ate->l_pages)
    return 0;

  /* setup local variables */
  page_size = vmm.page_size;
  flags     = __ate->flags;
  end       = __beg+__num;

  for (ip=__beg; ip<end; ++ip) {
    if (MMU_DIRTY == (flags[ip]&MMU_DIRTY)) {
      ret = mprotect((void*)(__ate->base+(ip*page_size)), page_size,\
        PROT_READ);
      if (-1 == ret)
        return -1;
    }
    /* flag: 0*0 */
    flags[ip] &= ~(MMU_DIRTY|MMU_ZFILL);
  }

  return 0;
}


/****************************************************************************/
/*! The SIGSEGV handler. */
/****************************************************************************/
static inline void
__vmm_sigsegv__(int const sig, siginfo_t * const si, void * const ctx)
{
  int ret;
  size_t ip, page_size, l_pages;
  ssize_t numrd;
  uintptr_t addr;
  uint8_t * flags;
  struct ate * ate;

  /* make sure we received a SIGSEGV */
  assert(SIGSEGV == sig);

  /* setup local variables */
  page_size = vmm.page_size;
  addr      = (uintptr_t)si->si_addr;

  /* lookup allocation table entry */
  ate = __mmu_lookup_ate__(&(vmm.mmu), (void*)addr);
  assert(NULL != ate);

  ip    = (addr-ate->base)/page_size;
  flags = ate->flags;

  if (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) {
    /* TODO: check memory file to see if there is enough free memory to
     * complete this allocation. */

    /* swap in the required memory */
    if (VMM_LZYRD == (vmm.opts&VMM_LZYRD)) {
      l_pages = 1;
      numrd   = __vmm_swap_i__(ate, ip, 1);
      assert(-1 != numrd);
    }
    else {
      l_pages = ate->n_pages-ate->l_pages;
      numrd   = __vmm_swap_i__(ate, 0, ate->n_pages);
      assert(-1 != numrd);
    }

    /* release lock on alloction table entry */
    ret = LOCK_LET(&(ate->lock));
    assert(-1 != ret);

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
    assert(MMU_DIRTY != (flags[ip]&MMU_DIRTY)); /* not dirty */

    /* flag: 100 */
    flags[ip] = MMU_DIRTY;

    /* update protection to read-write */
    ret = mprotect((void*)(ate->base+(ip*page_size)), page_size,\
      PROT_READ|PROT_WRITE);
    assert(-1 != ret);

    /* release lock on alloction table entry */
    ret = LOCK_LET(&(ate->lock));
    assert(-1 != ret);

    /* track number of write faults */
    __vmm_track__(numwf, 1);
  }

  if (NULL == ctx) {} /* suppress unused warning */
}


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
