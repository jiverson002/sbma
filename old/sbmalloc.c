#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#ifdef _BDMPI
# ifndef USE_PTHREAD
#   define USE_PTHREAD
# endif
#endif

#ifdef NDEBUG
# undef NDEBUG
# include <assert.h>      /* assert */
# define NDEBUG
#endif

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

#include "sbmalloc.h"     /* sbmalloc library */


#ifdef USE_PTHREAD
# include <pthread.h>   /* pthread library */
# include <syscall.h>   /* SYS_gettid, syscall */
# include <time.h>      /* CLOCK_REALTIME, struct timespec, clock_gettime */

# define SBDEADLOCK 0   /* 0: no deadlock diagnostics, */
                        /* 1: deadlock diagnostics */

# define _SB_GET_LOCK(LOCK)                                                 \
do {                                                                        \
  int ret = pthread_mutex_lock(LOCK);                                       \
  assert(0 == ret);                                                         \
  if (SBDEADLOCK) printf("[%5d] mtx get %s:%d %s (%p)\n",                   \
    (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK, (void*)(LOCK));    \
} while (0)

# define SB_INIT_LOCK(LOCK)                                                 \
do {                                                                        \
  int ret = pthread_mutex_init(LOCK, NULL);                                 \
  assert(0 == ret);                                                         \
} while (0)

# define SB_FREE_LOCK(LOCK)                                                 \
do {                                                                        \
  int ret = pthread_mutex_destroy(LOCK);                                    \
  assert(0 == ret);                                                         \
} while (0)

# if !defined(SBDEADLOACK) || SBDEADLOCK == 0
#   define SB_GET_LOCK(LOCK) _SB_GET_LOCK(LOCK)
# else
#   define SB_GET_LOCK(LOCK)                                                \
do {                                                                        \
  int ret;                                                                  \
  struct timespec ts;                                                       \
  ret = clock_gettime(CLOCK_REALTIME, &ts);                                 \
  assert(0 == ret);                                                         \
  ts.tv_sec += 10;                                                          \
  ret = pthread_mutex_timedlock(LOCK, &ts)));                               \
  if (ETIMEDOUT == ret) {                                                   \
    printf("[%5d] mtx lock timed-out %s:%d %s (%p)\n",                      \
      (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK, (void*)(LOCK));  \
    _SB_GET_LOCK(LOCK);                                                     \
  }                                                                         \
  assert(0 == ret || ETIMEDOUT == ret);                                     \
  if (SBDEADLOCK) printf("[%5d] mtx get %s:%d %s (%p)\n",                   \
    (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK, (void*)(LOCK));    \
} while (0)
# endif

# define SB_LET_LOCK(LOCK)                                                  \
do {                                                                        \
  int ret = pthread_mutex_unlock(LOCK);                                     \
  assert(0 == ret);                                                         \
  if (SBDEADLOCK) printf("[%5d] mtx let %s:%d %s (%p)\n",                   \
    (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK, (void*)(LOCK));    \
} while (0)
#else
# define SB_INIT_LOCK(LOCK)
# define SB_FREE_LOCK(LOCK)
# define SB_GET_LOCK(LOCK)
# define SB_LET_LOCK(LOCK)
#endif

#define SB_TO_SYS(NPAGES, PAGESIZE) \
  ((NPAGES)*(PAGESIZE)/sysconf(_SC_PAGESIZE))

#define SB_INIT_CHECK\
  if (0 == sb_info.init)\
    __info_init__();

#define MMAP_FLAGS MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE

#define SBMMAP(ADDR, LEN, PROT)                                             \
do {                                                                        \
  (ADDR) = (uintptr_t)mmap(NULL, LEN, PROT, MMAP_FLAGS, -1, 0);             \
  assert(MAP_FAILED != (void*)(ADDR));                                      \
} while (0)

#define SBMREMAP(ADDR, SIZE, NADDR)                                         \
do {                                                                        \
  void * SBMREMAP_addr = mremap((void*)(ADDR), SIZE, SIZE,                  \
    MREMAP_MAYMOVE|MREMAP_FIXED, NADDR);                                    \
  assert(MAP_FAILED != SBMREMAP_addr);                                      \
} while (0)

#define SBMUNMAP(ADDR, LEN)                                                 \
do {                                                                        \
  int ret = munmap((void*)(ADDR), LEN);                                     \
  assert(-1 != ret);                                                        \
} while (0)

#define SBMLOCK(ADDR, LEN)                                                  \
do {                                                                        \
  int ret = libc_mlock((void*)(ADDR), LEN);                                 \
  assert(-1 != ret);                                                        \
} while (0)

#define SBMUNLOCK(ADDR, LEN)                                                \
do {                                                                        \
  int ret = libc_munlock((void*)(ADDR), LEN);                               \
  assert(-1 != ret);                                                        \
} while (0)

#define SBMADVISE(ADDR, LEN, FLAG)                                          \
do {                                                                        \
  int ret = madvise((void*)(ADDR), LEN, FLAG);                              \
  assert(-1 != ret);                                                        \
} while (0)

#define SBMPROTECT(ADDR, LEN, PROT)                                         \
do {                                                                        \
  /*printf("[%5d]   %s %zu -- %zu\n", (int)getpid(),\
    0!=((PROT)&PROT_READ)?0!=((PROT)&PROT_WRITE)?"R/W":"R  ":"W  ",\
    (size_t)(ADDR), (size_t)(ADDR)+(LEN));*/\
  int ret = mprotect((void*)(ADDR), LEN, PROT);                             \
  assert(-1 != ret);                                                        \
} while (0)

#define SBFADVISE(FD, OFF, LEN, FLAG)                                       \
do {                                                                        \
  int ret = posix_fadvise(FD, OFF, LEN, FLAG);                              \
  assert(-1 != ret);                                                        \
} while (0)


/****************************************************************************/
/* Function prototypes for hooks. */
/****************************************************************************/
extern int     libc_open(char const * path, int flags, ...);
extern ssize_t libc_read(int const fd, void * const buf, size_t const count);
extern ssize_t libc_write(int const fd, void const * const buf, size_t const count);
extern int     libc_mlock(void const * const addr, size_t const len);
extern int     libc_munlock(void const * const addr, size_t const len);


/****************************************************************************/
/* Increments a particular field in the info struct. */
/****************************************************************************/
#define __INFO_TRACK__(FIELD, VAL)\
{\
  SB_GET_LOCK(&(sb_info.lock));\
  (FIELD) += (VAL);\
  SB_LET_LOCK(&(sb_info.lock));\
}


/****************************************************************************/
/*
 *  Page state code bits:
 *
 *    bit 0 ==    0:                     1: synchronized
 *    bit 1 ==    0: clean               1: dirty
 *    bit 2 ==    0:                     1: stored on disk
 */
/****************************************************************************/
enum sb_states
{
  SBPAGE_SYNC   = 1,
  SBPAGE_DIRTY  = 2,
  SBPAGE_ONDISK = 8
};


/****************************************************************************/
/* Stores information associated with an external memory allocation. */
/****************************************************************************/
struct sb_alloc
{
  uintptr_t base;         /* application handle to the mapping */

  size_t len;             /* number of application bytes */
  size_t m_pages;         /* number of meta info sbpages allocated */
  size_t n_pages;         /* number of application sbpages allocated */
  size_t ld_pages;        /* number of application sbpages loaded */

  char * flags;           /* per-page flags vector */

  char * fname;           /* the file that will store the data */

  struct sb_alloc * next; /* singly linked-list of allocations */

#ifdef USE_PTHREAD
  pthread_mutex_t lock;   /* mutex guarding struct */
#endif
};


/****************************************************************************/
/* Stores information associated with the external memory environment. */
/****************************************************************************/
static struct sb_info
{
  int init;                 /* initialized variable */

  size_t pagesize;          /* bytes per sbmalloc page */

  size_t numrf;             /* total number of read segfaults */
  size_t numwf;             /* total number of write segfaults */
  size_t numrd;             /* total number of pages read */
  size_t numwr;             /* total number of pages written */
  size_t curpages;          /* current pages loaded */
  size_t maxpages;          /* maximum number of pages allocated */
  size_t numpages;          /* current pages allocated */

  char fstem[FILENAME_MAX]; /* the file stem where the data is stored */

  struct sb_alloc * head;   /* singly linked-list of allocations */

  struct sigaction act;     /* for the SIGSEGV signal handler */
  struct sigaction oldact;  /* ... */

  int (*acct_charge_cb)(size_t);    /* function pointers for accounting */
  int (*acct_discharge_cb)(size_t); /* ... */

#ifdef USE_PTHREAD
  pthread_mutex_t init_lock;  /* mutex guarding initialization */
  pthread_mutex_t lock;       /* mutex guarding struct */
#endif
} sb_info = {
  .init              = 0,
  .numrf             = 0,
  .numwf             = 0,
  .numrd             = 0,
  .numwr             = 0,
  .curpages          = 0,
  .maxpages          = 0,
  .numpages          = 0,
  .fstem             = {'/', 't', 'm', 'p', '/', '\0'},
  .head              = NULL,
  .acct_charge_cb    = NULL,
  .acct_discharge_cb = NULL
#ifdef USE_PTHREAD
  ,
  .init_lock         = PTHREAD_MUTEX_INITIALIZER,
  .lock              = PTHREAD_MUTEX_INITIALIZER
#endif
};


/****************************************************************************/
/* User specified options. */
/****************************************************************************/
static int sb_opts[SBOPT_NUM]=
{
  [SBOPT_NUMPAGES] = 1,
  [SBOPT_LAZYREAD] = 0
};


/****************************************************************************/
/*! Read data from file. */
/****************************************************************************/
static inline int
__pread__(int const __fd, void * const __buf, size_t __len, size_t __off)
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
__pwrite__(int const __fd, void const * const __buf, size_t __len, size_t __off)
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
/*! Check for specific flags state. */
/****************************************************************************/
static inline int
__if_flags__(int const __flags, int const __yes, int const __no)
{
  if (__yes != (__flags&__yes))
    return 0;
  if (0 != (__flags&__no))
    return 0;
  return 1;
}


/****************************************************************************/
/*! Swaps the supplied range of sbpages in, reading any necessary sbpages from
 *  disk. */
/****************************************************************************/
static inline ssize_t
__swap_in__(struct sb_alloc * const __alloc, size_t const __beg,
            size_t const __num)
{
  int fd, ret;
  size_t ip, psize, end, numrd=0;
  ssize_t ipfirst;
  uintptr_t addr;
  char * flags;

  /* error check input values */
  if (NULL == __alloc)
    return -1;
  if (__num > __alloc->n_pages)
    return -1;
  if (__beg > __alloc->n_pages-__num)
    return -1;
  if (__num > __alloc->n_pages-__alloc->ld_pages)
    return -1;

  /* shortcut by checking to see if all pages are already loaded */
  if (__alloc->ld_pages == __alloc->n_pages)
    return 0;

  /* setup local variables */
  psize = sb_info.pagesize;
  flags = __alloc->flags;
  end   = __beg+__num;

#ifdef USE_PTHREAD
  /* mmap temporary memory with write protection for loading from disk */
  SBMMAP(addr, __num*psize, PROT_WRITE);
  /* lock temporary memory to prevent swapping -- this lock will be maintained
   * after the call to mremap according to mremap man page */
  SBMLOCK(addr, __num*psize);
#else
  addr = __alloc->base+(__beg*psize);
  SBMPROTECT(addr, __num*psize, PROT_WRITE);
#endif

  /* open the file for reading */
  fd = libc_open(__alloc->fname, O_RDONLY);
  assert(-1 != fd);

  /* load only those sbpages which were previously written to disk and have
   * not since been dumped */
  for (ipfirst=-1,ip=__beg; ip<=end; ++ip) {
    if (ip != end) {
      /* make sure no flags are set besides possibly SBPAGE_ONDISK */
      assert(__if_flags__(flags[ip], 0, ~SBPAGE_ONDISK));

      /* + SYNC flag */
      flags[ip] |= SBPAGE_SYNC;
    }

    if (ip != end && __if_flags__(flags[ip], SBPAGE_ONDISK, 0)) {
      if (-1 == ipfirst)
        ipfirst = ip;
    }
    else if (-1 != ipfirst) {
      ret = __pread__(fd, (void*)(addr+((ipfirst-__beg)*psize)),
        (ip-ipfirst)*psize, ipfirst*psize);
      assert(-1 != ret);

      numrd += (ip-ipfirst);

      ipfirst = -1;
    }
  }

  /* close file */
  ret = close(fd);
  assert(-1 != ret);

  /* update protection to read-only and remap the temporary memory into the
   * persistent memory */
  SBMPROTECT(addr, __num*psize, PROT_READ);
#ifdef USE_PTHREAD
  SBMREMAP(addr, __num*psize, __alloc->base+(__beg*psize));
#endif

  /* increment the number of loaded sbpages for the alloction */
  __alloc->ld_pages += __num;

  /* return the number of syspages read from disk */
  return SB_TO_SYS(numrd, psize);
}


/****************************************************************************/
/*! Swaps the supplied range of sbpages out, writing any dirty sbpages to
 *  disk. */
/****************************************************************************/
static inline ssize_t
__swap_out__(struct sb_alloc * const __alloc, size_t const __beg,
             size_t const __num)
{
  int fd, ret;
  size_t ip, psize, end, numwr=0;
  ssize_t ipfirst;
  uintptr_t addr;
  char * flags;

  /* error check input values */
  if (NULL == __alloc)
    return -1;
  if (__num > __alloc->n_pages)
    return -1;
  if (__beg > __alloc->n_pages-__num)
    return -1;

  /* shortcut by checking to see if no pages are currently loaded */
  /* TODO: if we track the number of dirty pages, then this can do a better
   * job of short-cutting */
  if (0 == __alloc->ld_pages)
    return 0;

  /* setup local variables */
  psize = sb_info.pagesize;
  addr  = __alloc->base;
  flags = __alloc->flags;
  end   = __beg+__num;

  /* open the file for writing */
  fd = libc_open(__alloc->fname, O_WRONLY);
  assert(-1 != fd);

  /* go over the pages and write the ones that have changed. perform the
   * writes in contigous chunks of changed pages. */
  for (ipfirst=-1,ip=__beg; ip<=end; ++ip) {
    if (ip != end && __if_flags__(flags[ip], 0, SBPAGE_DIRTY)) {
      /* make sure that flags besides possibly SBPAGE_SYNC or SBPAGE_ONDISK */
      assert(__if_flags__(flags[ip], 0, ~(SBPAGE_SYNC|SBPAGE_ONDISK)));

      if (__if_flags__(flags[ip], SBPAGE_SYNC, 0)) {
        assert(__alloc->ld_pages > 0);
        __alloc->ld_pages--;
      }

      /* - all flags except ONDISK */
      flags[ip] &= SBPAGE_ONDISK;
    }

    if (ip != end && __if_flags__(flags[ip], SBPAGE_DIRTY, 0)) {
      /* make sure that flags besides possibly SBPAGE_DIRTY or SBPAGE_ONDISK */
      assert(__if_flags__(flags[ip], 0, ~(SBPAGE_DIRTY|SBPAGE_ONDISK)));
      if (-1 == ipfirst)
        ipfirst = ip;

      assert(__alloc->ld_pages > 0);
      __alloc->ld_pages--;

      flags[ip] = SBPAGE_ONDISK;
    }
    else if (-1 != ipfirst) {
      ret = __pwrite__(fd, (void*)(addr+(ipfirst*psize)), (ip-ipfirst)*psize,
        ipfirst*psize);
      assert(-1 != ret);

      numwr += (ip-ipfirst);

      ipfirst = -1;
    }
  }

  /* close file */
  ret = close(fd);
  assert(-1 != ret);

  /* unlock the memory, update its protection to none and advise kernel to
   * release its associated resources */
  SBMUNLOCK(addr+(__beg*psize), __num*psize);
  SBMPROTECT(addr+(__beg*psize), __num*psize, PROT_NONE);
  SBMADVISE(addr+(__beg*psize), __num*psize, MADV_DONTNEED);

  /* return the number of syspages written to disk */
  return SB_TO_SYS(numwr, psize);
}


/****************************************************************************/
/*! Clear the SBPAGE_DIRTY and SBPAGE_ONDISK flags for the supplied range of
 *  sbpages */
/****************************************************************************/
static inline ssize_t
__swap_clr__(struct sb_alloc * const __alloc, size_t const __beg,
             size_t const __num)
{
  size_t ip, end, psize;
  char * flags;

  /* error check input values */
  if (NULL == __alloc)
    return -1;
  if (__num > __alloc->n_pages)
    return -1;
  if (__beg > __alloc->n_pages-__num)
    return -1;
  if (__num > __alloc->ld_pages)
    return -1;

  /* shortcut by checking to see if no pages are currently loaded */
  /* TODO: if we track the number of dirty pages, then this can do a better
   * job of short-cutting */
  if (0 == __alloc->ld_pages)
    return 0;

  /* setup local variables */
  psize = sb_info.pagesize;
  flags = __alloc->flags;
  end   = __beg+__num;

  for (ip=__beg; ip<end; ++ip) {
    if (__if_flags__(flags[ip], SBPAGE_DIRTY, 0)) {
      /* - DIRTY/ONDISK */
      /* + SYNC */
      flags[ip] = SBPAGE_SYNC;

      SBMPROTECT(__alloc->base+(ip*psize), psize, PROT_READ);
    }
    else {
      /* - ONDISK */
      flags[ip] &= ~SBPAGE_ONDISK;
    }
  }

  return 0;
}


/****************************************************************************/
/*! Returns a pointer to an sb_alloc that contains the specified
 *  address. */
/****************************************************************************/
static inline struct sb_alloc *
__info_find__(uintptr_t const __addr)
{
  size_t len;
  uintptr_t addr;
  struct sb_alloc * alloc;

  SB_GET_LOCK(&(sb_info.lock));
  for (alloc=sb_info.head; NULL!=alloc; alloc=alloc->next) {
    len  = alloc->len;
    addr = alloc->base;
    if (__addr >= addr && __addr < addr+len)
      break;
  }
  SB_LET_LOCK(&(sb_info.lock));

  return alloc;
}


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


/****************************************************************************/
/*! Initializes the sbmalloc subsystem. */
/****************************************************************************/
static inline void
__info_init__(void)
{
  int ret;

  SB_GET_LOCK(&(sb_info.init_lock));

  if (1 == sb_info.init) {
    SB_LET_LOCK(&(sb_info.init_lock));
    return;
  }

  sb_info.init     = 1;
  sb_info.pagesize = sb_opts[SBOPT_NUMPAGES]*sysconf(_SC_PAGESIZE);

  /* setup the signal handler */
  sb_info.act.sa_flags     = SA_SIGINFO;
  sb_info.act.sa_sigaction = __info_segv__;
  ret = sigemptyset(&(sb_info.act.sa_mask));
  assert(-1 != ret);
  ret = sigaction(SIGSEGV, &(sb_info.act), &(sb_info.oldact));
  assert(-1 != ret);

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

  SB_LET_LOCK(&(sb_info.init_lock));
}


/****************************************************************************/
/*! Shuts down the sbmalloc subsystem. */
/****************************************************************************/
static inline void
__info_destroy__(void)
{
  int ret;

  SB_GET_LOCK(&(sb_info.init_lock));

  if (0 == sb_info.init) {
    SB_LET_LOCK(&(sb_info.init_lock));
    return;
  }

  sb_info.init = 0;

  ret = sigaction(SIGSEGV, &(sb_info.oldact), NULL);
  assert(-1 != ret);

  SB_LET_LOCK(&(sb_info.init_lock));
}


/****************************************************************************/
/*! Allocate memory via anonymous mmap. */
/****************************************************************************/
extern void *
SB_mmap(size_t const __len)
{
  int fd=-1;
  size_t ip, psize, m_len, m_pages, ld_pages;
  uintptr_t addr=(uintptr_t)MAP_FAILED;
  char * fname, * flags;
  struct sb_alloc * alloc;

  SB_INIT_CHECK

  /* shortcut */
  if (0 == __len)
    return NULL;

  /* compute allocation sizes */
  psize    = sb_info.pagesize;
  ld_pages = 1+((__len-1)/psize);
  m_len    = (sizeof(struct sb_alloc))+ld_pages+(100+strlen(sb_info.fstem));
  m_pages  = 1+((m_len-1)/psize);

  /* invoke charge callback function before allocating any memory */
  if (NULL != sb_info.acct_charge_cb)
    (void)(*sb_info.acct_charge_cb)(SB_TO_SYS(m_pages+ld_pages, psize));

  /* allocate memory with read-only protection -- this will avoid the double
   * SIGSEGV for new allocations */
  SBMMAP(addr, (m_pages+ld_pages)*psize, PROT_READ);

  /* read/write protect meta information */
  SBMPROTECT(addr+(ld_pages*psize), m_pages*psize, PROT_READ|PROT_WRITE);
  SBMLOCK(addr+(ld_pages*psize), m_pages*psize);

  /* set pointer for the allocation structure */
  alloc = (struct sb_alloc*)(addr+(ld_pages*psize));
  /* set pointer for the per-page flag vector */
  flags = (char*)((uintptr_t)alloc+sizeof(struct sb_alloc));
  /* set pointer for the filename for storage purposes */
  fname = (char*)((uintptr_t)flags+ld_pages);

  for (ip=0; ip<ld_pages; ++ip)
    flags[ip] = SBPAGE_SYNC;

  /* create and truncate the file to size */
  if (0 > sprintf(fname, "%s%d-%p", sb_info.fstem, (int)getpid(),
      (void*)alloc))
  {
    goto CLEANUP;
  }
  if (-1 == (fd=libc_open(fname, O_RDWR|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR))) {
    goto CLEANUP;
  }
  if (-1 == ftruncate(fd, ld_pages*psize)) {
    goto CLEANUP;
  }
  if (-1 == close(fd)) {
    goto CLEANUP;
  }
  /* fd = -1; */ /* Only need if a goto CLEANUP follows */

  /* populate sb_alloc structure */
  alloc->m_pages  = m_pages;  /* number of meta sbpages */
  alloc->n_pages  = ld_pages; /* number application of sbpages */
  alloc->ld_pages = ld_pages; /* number of loaded sbpages */
  alloc->len      = __len;    /* number of application bytes */
  alloc->fname    = fname;
  alloc->base     = addr;
  alloc->flags    = flags;

  /* initialize sb_alloc lock */
  SB_INIT_LOCK(&(alloc->lock));

  /* add to linked-list */
  SB_GET_LOCK(&(sb_info.lock));
  alloc->next  = sb_info.head;
  sb_info.head = alloc;
  SB_LET_LOCK(&(sb_info.lock));

  /* track number of syspages currently loaded, high water mark number of
   * syspages loaded and syspages currently allocated */
  __INFO_TRACK__(sb_info.curpages, SB_TO_SYS(m_pages+ld_pages, psize));
  __INFO_TRACK__(sb_info.maxpages,
    sb_info.curpages>sb_info.maxpages?sb_info.curpages-sb_info.maxpages:0);
  __INFO_TRACK__(sb_info.numpages, SB_TO_SYS(m_pages+ld_pages, psize));

  return (void *)alloc->base;

CLEANUP:
  if (MAP_FAILED != (void*)addr)
    SBMUNMAP(addr, (m_pages*ld_pages)*psize);
  if (-1 != fd)
    (void)close(fd);
  return NULL;
}


/****************************************************************************/
/*! Frees the memory associated with an anonymous mmap. */
/****************************************************************************/
extern void
SB_munmap(void * const __addr, size_t const __len)
{
  int ret;
  size_t psize, m_pages, n_pages, ld_pages;
  struct sb_alloc * alloc, * palloc=NULL;

  SB_INIT_CHECK

  /* TODO: we could have api like munmap(void *, size_t) then just assume that
   * a correct address is being passed and treat it as such -- this would
   * avoid the need for search through the info struct. this would require
   * that the info struct was doubly-linked. */

  SB_GET_LOCK(&(sb_info.lock));
  for (alloc=sb_info.head; NULL!=alloc; alloc=alloc->next) {
    if (alloc->base == (uintptr_t)__addr)
      break;
    palloc = alloc;
  }
  assert(NULL != alloc);

  /* update the link-list */
  if (NULL == palloc)
    sb_info.head = alloc->next;
  else
    palloc->next = alloc->next;
  SB_LET_LOCK(&(sb_info.lock));

  psize    = sb_info.pagesize;
  m_pages  = alloc->m_pages;
  n_pages  = alloc->n_pages;
  ld_pages = alloc->ld_pages;

  /* track number of syspages currently loaded and allocated */
  __INFO_TRACK__(sb_info.curpages, -SB_TO_SYS(m_pages+ld_pages, psize));
  __INFO_TRACK__(sb_info.numpages, -SB_TO_SYS(m_pages+n_pages, psize));

  ret = unlink(alloc->fname);
  assert(-1 != ret);

  /* free resources */
  SB_FREE_LOCK(&(alloc->lock));
  SBMUNLOCK(alloc->base, (m_pages+n_pages)*psize);
  SBMUNMAP(alloc->base, (m_pages+n_pages)*psize);

  /* invoke discharge callback function after releasing memory */
  if (NULL != sb_info.acct_discharge_cb)
    (void)(*sb_info.acct_discharge_cb)(SB_TO_SYS(m_pages+ld_pages, psize));

  if (0 == __len) {}
}


/****************************************************************************/
/*! Touch the specified range. */
/****************************************************************************/
extern ssize_t
SB_mtouch(void * const __addr, size_t __len)
{
  size_t ip, beg, end, psize, num, ld_pages, numrd;
  uintptr_t addr;
  char * flags;
  struct sb_alloc * alloc;

  SB_INIT_CHECK

  if (NULL == (alloc=__info_find__((uintptr_t)__addr)))
    return -1;

  SB_GET_LOCK(&(alloc->lock));

  psize = sb_info.pagesize;
  flags = alloc->flags;

  if (alloc->len < __len)
    __len = alloc->base+alloc->len-(uintptr_t)__addr;

  if (0 == sb_opts[SBOPT_LAZYREAD]) {
    beg = 0;
    end = alloc->n_pages;
  }
  else {
    /* need to make sure that all bytes are captured, thus beg is a floor
     * operation and end is a ceil operation. */
    addr = alloc->base;
    beg  = ((uintptr_t)__addr-addr)/psize;
    end  = ((uintptr_t)__addr+__len == addr+alloc->len)
             ? alloc->n_pages
             : 1+(((uintptr_t)__addr+__len-addr-1)/psize);
  }

  /* invoke charge callback function before swapping in any memory -- this
   * is only done once for each allocation between successive swap outs */
  if (0 == alloc->ld_pages && NULL != sb_info.acct_charge_cb)
    (void)(*sb_info.acct_charge_cb)(SB_TO_SYS(alloc->n_pages, psize));

  /* scan the supplied range and swap in any necessary pages */
  for (numrd=0,ld_pages=0,ip=beg; ip<end; ++ip) {
    if (__if_flags__(flags[ip], 0, SBPAGE_SYNC|SBPAGE_DIRTY)) {
      num    = __swap_in__(alloc, ip, 1);
      assert(-1 != num);
      numrd += num;
      ld_pages++;
    }
  }

  SB_LET_LOCK(&(alloc->lock));

  /* track number of syspages currently loaded, number of syspages written to
   * disk, and high water mark for syspages loaded */
  __INFO_TRACK__(sb_info.curpages, SB_TO_SYS(ld_pages, psize));
  __INFO_TRACK__(sb_info.numrd, numrd);
  __INFO_TRACK__(sb_info.maxpages,
    sb_info.curpages>sb_info.maxpages?sb_info.curpages-sb_info.maxpages:0);

  return SB_TO_SYS(ld_pages, psize);
}


/****************************************************************************/
/*! Touch all allocations. */
/****************************************************************************/
extern ssize_t
SB_mtouchall(void)
{
  size_t psize, ld_pages=0;
  struct sb_alloc * alloc;

  SB_INIT_CHECK

  SB_GET_LOCK(&(sb_info.lock));

  psize = sb_info.pagesize;

  for (alloc=sb_info.head; NULL!=alloc; alloc=alloc->next)
    ld_pages += SB_mtouch((void*)alloc->base, SIZE_MAX);
  SB_LET_LOCK(&(sb_info.lock));

  return SB_TO_SYS(ld_pages, psize);
}


/****************************************************************************/
/*! Clear the specified range. */
/****************************************************************************/
extern ssize_t
SB_mclear(void * const __addr, size_t __len)
{
  size_t beg, end, psize;
  uintptr_t addr;
  struct sb_alloc * alloc;

  SB_INIT_CHECK

  if (NULL == (alloc=__info_find__((uintptr_t)__addr)))
    return -1;

  SB_GET_LOCK(&(alloc->lock));

  psize = sb_info.pagesize;

  if (alloc->len < __len)
    __len = alloc->base+alloc->len-(uintptr_t)__addr;

  addr = alloc->base;
  /* can only clear pages fully within range, thus beg is a ceil
   * operation and end is a floor operation, except for when addr+len
   * consumes all of the last page, then end just equals n_pages. */
  beg = ((uintptr_t)__addr == addr)
          ? 0
          : 1+(((uintptr_t)__addr-addr-1)/psize);
  end = ((uintptr_t)__addr+__len == addr+alloc->len)
          ? alloc->n_pages
          : ((uintptr_t)__addr+__len-addr)/psize;

  (void)__swap_clr__(alloc, beg, end-beg);

  SB_LET_LOCK(&(alloc->lock));

  return 0;
}


/****************************************************************************/
/*! Clear all allocations. */
/****************************************************************************/
extern ssize_t
SB_mclearall(void)
{
  struct sb_alloc * alloc;

  SB_INIT_CHECK

  SB_GET_LOCK(&(sb_info.lock));

  for (alloc=sb_info.head; NULL!=alloc; alloc=alloc->next) {
    SB_GET_LOCK(&(alloc->lock));
    (void)__swap_clr__(alloc, 0, alloc->n_pages);
    SB_LET_LOCK(&(alloc->lock));
  }
  SB_LET_LOCK(&(sb_info.lock));

  return 0;
}


/****************************************************************************/
/*! Evict the allocation containing addr. */
/****************************************************************************/
extern ssize_t
SB_mevict(void * const __addr)
{
  size_t psize, ld_pages, numwr;
  struct sb_alloc * alloc;

  SB_INIT_CHECK

  if (NULL == (alloc=__info_find__((uintptr_t)__addr)))
    return -1;

  psize = sb_info.pagesize;

  SB_GET_LOCK(&(alloc->lock));

  ld_pages = alloc->ld_pages;
  numwr    = __swap_out__(alloc, 0, alloc->n_pages);
  assert(-1 != numwr);
  assert(0 == alloc->ld_pages);

  SB_LET_LOCK(&(alloc->lock));

  /* track number of syspages currently loaded and number of syspages
   * written to disk */
  __INFO_TRACK__(sb_info.curpages, -SB_TO_SYS(ld_pages, psize));
  __INFO_TRACK__(sb_info.numwr, numwr);

  /* invoke discharge callback function after swapping out memory */
  if (NULL != sb_info.acct_discharge_cb)
    (void)(*sb_info.acct_discharge_cb)(SB_TO_SYS(ld_pages, psize));

  return SB_TO_SYS(ld_pages, psize);
}


/****************************************************************************/
/*! Evict all allocations. */
/****************************************************************************/
extern ssize_t
SB_mevictall(void)
{
  size_t psize, num, ld_pages=0, numwr=0;
  struct sb_alloc * alloc;

  SB_INIT_CHECK

  SB_GET_LOCK(&(sb_info.lock));
  psize = sb_info.pagesize;
  for (alloc=sb_info.head; NULL!=alloc; alloc=alloc->next) {
    SB_GET_LOCK(&(alloc->lock));
    ld_pages += alloc->ld_pages;
    num       = __swap_out__(alloc, 0, alloc->n_pages);
    numwr    += num;
    assert(-1 != num);
    assert(0 == alloc->ld_pages);
    SB_LET_LOCK(&(alloc->lock));
  }
  SB_LET_LOCK(&(sb_info.lock));

  /* track number of syspages currently loaded and number of syspages
   * written to disk */
  __INFO_TRACK__(sb_info.curpages, -SB_TO_SYS(ld_pages, psize));
  __INFO_TRACK__(sb_info.numwr, numwr);

  /* invoke discharge callback function after swapping out memory */
  if (NULL != sb_info.acct_discharge_cb)
    (void)(*sb_info.acct_discharge_cb)(SB_TO_SYS(ld_pages, psize));

  return SB_TO_SYS(ld_pages, psize);
}


/****************************************************************************/
/*! Check if addr exists in sbmalloc managed allocation. */
/****************************************************************************/
extern int
SB_mexist(void const * const __addr)
{
  SB_INIT_CHECK

  return (NULL != __info_find__((uintptr_t)__addr));
}


/****************************************************************************/
/*! Set parameters for the sbmalloc subsystem. */
/****************************************************************************/
extern int
SB_mopt(int const param, int const value)
{
  if (param >= SBOPT_NUM)
    return -1;

  sb_opts[param] = value;

  return 0;
}


/****************************************************************************/
/*! Set functions for sbmalloc accounting system */
/****************************************************************************/
extern int
SB_mcal(int (*acct_charge_cb)(size_t), int (*acct_discharge_cb)(size_t))
{
  SB_GET_LOCK(&(sb_info.lock));
  sb_info.acct_charge_cb    = acct_charge_cb;
  sb_info.acct_discharge_cb = acct_discharge_cb;
  SB_LET_LOCK(&(sb_info.lock));

  return 0;
}


/****************************************************************************/
/*! Set parameters for the sbmalloc subsystem. */
/****************************************************************************/
extern int
SB_mfile(char const * const file)
{
  SB_GET_LOCK(&(sb_info.lock));
  strncpy(sb_info.fstem, file, FILENAME_MAX-1);
  sb_info.fstem[FILENAME_MAX-1] = '\0';
  SB_LET_LOCK(&(sb_info.lock));

  return 0;
}


/****************************************************************************/
/* Return some memory statistics */
/****************************************************************************/
extern struct mallinfo
SB_minfo(void)
{
  struct mallinfo mi;

  SB_GET_LOCK(&(sb_info.lock));
  mi.smblks  = sb_info.numrf; /* read faults */
  mi.ordblks = sb_info.numwf; /* write faults */

  mi.usmblks = sb_info.numrd; /* syspages read from disk */
  mi.fsmblks = sb_info.numwr; /* syspages wrote to disk */

  mi.uordblks = sb_info.curpages; /* syspages loaded */
  mi.fordblks = sb_info.maxpages; /* high water mark for loaded syspages */
  mi.keepcost = sb_info.numpages; /* syspages allocated */
  SB_LET_LOCK(&(sb_info.lock));

  return mi;
}


/****************************************************************************/
/*! Frees the memory associated with an anonymous mmap. */
/****************************************************************************/
extern void
SB_init(void)
{
  __info_init__();
}


/****************************************************************************/
/*! Frees the memory associated with an anonymous mmap. */
/****************************************************************************/
extern void
SB_finalize(void)
{
  struct sb_alloc * alloc;

  __info_destroy__();
}


/****************************************************************************/
/*! Report swap usage. */
/****************************************************************************/
extern int
SB_swap_usage(int const tag)
{
  int fd=-1, retval=-1;
  size_t size, used;
  char * tok;
  char buf[16384], file[FILENAME_MAX];

  if (-1 == (fd=libc_open("/proc/swaps", O_RDONLY)))
    goto CLEANUP;

  if (-1 == libc_read(fd, buf, sizeof(buf)))
    goto CLEANUP;

  /* skip header line */
  tok = strtok(buf, "\n");

  if (NULL != tok) {
    /* loop through swap lines */
    tok = strtok(NULL, "\n");
    while (NULL != tok) {
      if (3 != sscanf(tok, "%s %*s %zu %zu %*s", file, &size, &used)) {
        goto CLEANUP;
      }
      if (0 > printf("[%5d:%d] swap usage on %s: %zu / %zu\n", (int)getpid(),
        tag, file, used, size))
      {
        goto CLEANUP;
      }
      tok = strtok(NULL, "\n");
    }
  }

  retval = 0;

CLEANUP:
  /*if (-1 == retval)
    printf("swap_usage failed (%s)\n", strerror(errno));*/
  if (-1 != fd)
    close(fd);
  return retval;
}
