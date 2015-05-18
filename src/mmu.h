#ifndef __MMU_H__
#define __MMU_H__ 1


#include <assert.h>   /* assert library */
#include <stdarg.h>   /* stdarg library */
#include <stddef.h>   /* size_t */
#include <stdint.h>   /* uintptr_t */
#include <string.h>   /* memset */
#include <sys/mman.h> /* mmap, munmap */
#include <unistd.h>   /* sysconf */


/* TODO: locks MUST be recursive */
#ifdef USE_PTHREAD
# include <pthread.h>   /* pthread library */
# define LOCK_INIT(__LOCK) pthread_mutex_init(__LOCK, NULL)
# define LOCK_FREE(__LOCK) pthread_mutex_destroy(__LOCK)
# define LOCK_GET(__LOCK)  pthread_mutex_lock(__LOCK)
# define LOCK_LET(__LOCK)  pthread_mutex_unlock(__LOCK)
#else
# define LOCK_INIT(__LOCK) 0
# define LOCK_FREE(__LOCK) 0
# define LOCK_GET(__LOCK)  0
# define LOCK_LET(__LOCK)  0
#endif

#define MMU_MAX_MEM  (1lu<<40)
#define MMU_MIN_ADDR ((void*)(1lu<<16))
#define MMU_MAX_ADDR ((void*)((uintptr_t)MMU_MIN_ADDR+MMU_MAX_MEM-1))

#define MMAP_PROT  PROT_READ|PROT_WRITE
#define MMAP_FLAGS MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_LOCKED


/*
 * Memory management unit page status code bits:
 *
 *   bit 0 ==    0: page not allocated            1: page is allocated
 *   bit 1 ==    0: page must be filled from disk 1: zero fill allowed
 *   bit 2 ==    0: page is not resident          1: page is resident
 *   bit 3 ==    0: page is unmodified            1: page is dirty
 */
enum mmu_status_code
{
  MMU_ALLOC = 1 << 0,
  MMU_ZFILL = 1 << 1,
  MMU_RSDNT = 1 << 2,
  MMU_DIRTY = 1 << 3
};
#define MMU_NOADMIT  0
#define MMU_ADMIT    1
#define MMU_NOFLUSH  0
#define MMU_FLUSH    1


struct ate
{
  size_t len;           /*!< number of application bytes */
  size_t n_pages;       /*!< number of application sbpages allocated */
  size_t l_pages;       /*!< number of application sbpages loaded */
  void * base;          /*!< application handle to the mapping */
#ifdef USE_PTHREAD
  pthread_mutex_t lock; /* mutex guarding struct */
#endif
};


struct mmu
{
  size_t page_size;            /*!< mmu page size */
  size_t min_alloc_size;       /*!< mmu minimum allocation size */
  char * page_table;           /*!< mmu page table */
  struct ate * alloc_table;    /*!< mmu allocation table */
#ifdef USE_PTHREAD
  pthread_mutex_t lock;        /*!< mutex guarding struct */
#endif
};


static inline int
__mmu_init__(struct mmu * const __mmu, size_t const __page_size,
             size_t const __min_alloc_size)
{
  int ret;
  size_t pt_len, at_len;

  assert(0 == (__page_size&1));             /* must be a multiple of 2 */
  assert(0 == (__min_alloc_size&1));        /* ... */
  assert(__min_alloc_size >= __page_size);

  /* initialize variables */
  ret                = -1;
  __mmu->page_table  = MAP_FAILED;
  __mmu->alloc_table = MAP_FAILED;

  /* compute table lengths */
  pt_len = MMU_MAX_MEM/__page_size*sizeof(char)/2;
  at_len = (MMU_MAX_MEM/__min_alloc_size)*sizeof(struct ate);

  /* populate mmu variables */
  __mmu->page_size      = __page_size;
  __mmu->min_alloc_size = __min_alloc_size;
  __mmu->page_table     = (char*)mmap(NULL, pt_len, MMAP_PROT, MMAP_FLAGS, -1, 0);
  __mmu->alloc_table    = (struct ate*)mmap(NULL, at_len, MMAP_PROT, MMAP_FLAGS, -1, 0);
  ret                   = LOCK_INIT(&(__mmu->lock));

  if (MAP_FAILED != __mmu->page_table &&\
      MAP_FAILED != __mmu->alloc_table &&\
      -1 != ret)
  {
    return 0;
  }

  if (MAP_FAILED != __mmu->page_table)
    (void)munmap(__mmu->page_table, pt_len);
  if (MAP_FAILED != __mmu->alloc_table)
    (void)munmap(__mmu->alloc_table, at_len);
  if (0 == ret)
    (void)LOCK_FREE(&(__mmu->lock));

  return -1;
}

static inline int
__mmu_destroy__(struct mmu * const __mmu)
{
  size_t pt_len, at_len;

  /* compute table lengths */
  pt_len = MMU_MAX_MEM/__mmu->page_size*sizeof(char)/2;
  at_len = MMU_MAX_MEM/__mmu->min_alloc_size*sizeof(struct ate);

  (void)munmap(__mmu->page_table, pt_len);
  (void)munmap(__mmu->alloc_table, at_len);
  (void)LOCK_FREE(&(__mmu->lock));

  return 0;
}


static inline struct ate *
__mmu_new_ate__(struct mmu const * const __mmu, void const * const __addr,
                size_t const __len, int const __flag)
{
  int ret, flag;
  size_t i, idx, beg, end;
  struct ate * ate;

  assert(__addr >= MMU_MIN_ADDR);
  assert(__addr <= MMU_MAX_ADDR);

  idx = (size_t)(((uintptr_t)__addr-(uintptr_t)MMU_MIN_ADDR)/__mmu->min_alloc_size);
  ate = __mmu->alloc_table+idx;

  assert(0 == ate->base);
  assert(0 == ate->len);
  assert(0 == ate->n_pages);
  assert(0 == ate->l_pages);

  ret = LOCK_INIT(&(ate->lock));
  if (-1 == ret)
    return NULL;

  ate->base    = (void*)__addr;
  ate->len     = __len;
  ate->n_pages = 1+((__len-1)/__mmu->page_size);
  ate->l_pages = ate->n_pages;

  beg  = (size_t)(((uintptr_t)__addr-(uintptr_t)MMU_MIN_ADDR)/__mmu->page_size);
  end  = beg+ate->n_pages;
  flag = __flag | MMU_ALLOC;
  for (i=beg; i<end; ++i) {
    if (0 == (i & 1)) {
      assert(0 == (__mmu->page_table[i/2] >> 4));
      __mmu->page_table[i/2] = (__mmu->page_table[i/2] & 0x0F) | (flag << 4);
    }
    else {
      assert(0 == (__mmu->page_table[i/2] & 0x0F));
      __mmu->page_table[i/2] = (__mmu->page_table[i/2] & 0xF0) | flag;
    }
  }

  return ate;
}

static inline int
__mmu_del_ate__(struct mmu const * const __mmu, void const * const __addr,
                size_t const __len)
{
  int ret;
  size_t idx;
#ifndef NDEBUG
  size_t i, beg, end;
#endif
  struct ate * ate;

  assert(__addr >= MMU_MIN_ADDR);
  assert(__addr <= MMU_MAX_ADDR);

  idx = (size_t)(((uintptr_t)__addr-(uintptr_t)MMU_MIN_ADDR)/__mmu->min_alloc_size);
  ate = __mmu->alloc_table+idx;

  assert(__addr == ate->base);
  assert(__len == ate->len);
  assert(1+((__len-1)/__mmu->page_size) == ate->n_pages);
  assert(ate->n_pages == ate->l_pages);

#ifndef NDEBUG
  beg = (size_t)(((uintptr_t)__addr-(uintptr_t)MMU_MIN_ADDR)/__mmu->page_size);
  end = beg+ate->n_pages;
  for (i=beg; i<end; ++i) {
    if (0 == (i & 1))
      __mmu->page_table[i/2] = (__mmu->page_table[i/2] & 0x0F);
    else
      __mmu->page_table[i/2] = (__mmu->page_table[i/2] & 0xF0);
  }
#endif

  ate->base    = (void*)0;
  ate->len     = 0;
  ate->n_pages = 0;
  ate->l_pages = 0;

  ret = LOCK_FREE(&(ate->lock));
  if (-1 == ret)
    return -1;

  return 0;
}

static inline struct ate *
__mmu_get_ate__(struct mmu const * const __mmu, void const * const __addr)
{
  size_t idx;
  struct ate * ate;

  assert(__addr >= MMU_MIN_ADDR);
  assert(__addr <= MMU_MAX_ADDR);

  idx = (size_t)(((uintptr_t)__addr-(uintptr_t)MMU_MIN_ADDR)/__mmu->min_alloc_size);

  idx++;
  do {
    idx--;
    ate = __mmu->alloc_table+idx;
  } while (0 != idx &&\
    (ate->base > __addr || __addr >= (void*)((uintptr_t)ate->base+ate->len)));

  return\
    (ate->base <= __addr && __addr < (void*)((uintptr_t)ate->base+ate->len))\
      ? ate
      : NULL;
}


static inline int
__mmu_get_flag__(struct mmu const * const __mmu, size_t const __idx,
                 int const __field)
{
  assert(__field <= 0x0F);
  assert(__idx <= (size_t)((uintptr_t)MMU_MAX_ADDR/__mmu->page_size));

  return (0 == (__idx & 1))
    ? (__field == ((__mmu->page_table[__idx/2] >> 4) & __field))
    : (__field == ((__mmu->page_table[__idx/2] & 0x0F) & __field));
}
static inline void
__mmu_set_flag__(struct mmu * const __mmu, size_t const __idx,
                 int const __flag)
{
  assert(__flag <= 0x0F);
  assert(__idx <= (size_t)((uintptr_t)MMU_MAX_ADDR/__mmu->page_size));

  if (0 == (__idx & 1))
    __mmu->page_table[__idx/2] |= (__flag << 4);
  else
    __mmu->page_table[__idx/2] |= __flag;
}
static inline int
__mmu_unset_flag__(struct mmu * const __mmu, size_t const __idx,
                   int const __flag)
{
  assert(__flag <= 0x0F);
  assert(__idx <= (size_t)((uintptr_t)MMU_MAX_ADDR/__mmu->page_size));

  if (0 == (__idx & 1))
    __mmu->page_table[__idx/2] &= (~(__flag << 4));
  else
    __mmu->page_table[__idx/2] &= (~(__flag));

  return 0;
}


static inline size_t
__mmu_get_idx__(struct mmu const * const __mmu, void const * const __addr)
{
  assert(__addr >= MMU_MIN_ADDR);
  assert(__addr <= MMU_MAX_ADDR);

  return (size_t)(((uintptr_t)__addr-(uintptr_t)MMU_MIN_ADDR)/__mmu->page_size);
}


static inline struct ate *
__mmu_lock_ate__(struct mmu const * const __mmu, void const * const __addr)
{
  int ret;
  struct ate * ate;

  assert(__addr >= MMU_MIN_ADDR);
  assert(__addr <= MMU_MAX_ADDR);

  ret = LOCK_GET(&(__mmu->lock));
  if (-1 == ret)
    return NULL;

  ate = __mmu_get_ate__(__mmu, __addr);
  assert(NULL != ate);

  ret = LOCK_GET(&(ate->lock));
  if (-1 == ret) {
    (void)LOCK_LET(&(__mmu->lock));
    return NULL;
  }

  ret = LOCK_LET(&(__mmu->lock));
  if (-1 == ret)
    return NULL;

  return ate;
}


static inline int
__mmu_unlock_ate__(struct mmu const * const __mmu, struct ate * const __ate)
{
  int ret;

  ret = LOCK_LET(&(__ate->lock));
  if (-1 == ret)
    return -1;

  return 0;

  if (NULL == __mmu || NULL == __ate) {}
}


static inline size_t
__mmu_to_sys__(struct mmu const * const __mmu, size_t const __num)
{
  return (__num*__mmu->page_size)/sysconf(_SC_PAGESIZE);
}


/****************************************************************************/
/*! Read data from file. */
/****************************************************************************/
static inline int
__mmu_read__(int const __fd, void * const __buf, size_t __len, size_t __off)
{
  ssize_t len;
  char * buf = (char*)__buf;

#ifndef HAVE_PREAD
  if (-1 == lseek(__fd, __off, SEEK_SET))
    return -1;
#endif

  do {
#if 0
#ifdef HAVE_PREAD
    if (-1 == (len=libc_pread(__fd, buf, __len, __off)))
      return -1;
    __off += len;
#else
    if (-1 == (len=libc_read(__fd, buf, __len)))
      return -1;
#endif
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
__mmu_write__(int const __fd, void const * const __buf, size_t __len,
              size_t __off)
{
  ssize_t len;
  char * buf = (char*)__buf;

#ifndef HAVE_PWRITE
  if (-1 == lseek(__fd, __off, SEEK_SET))
    return -1;
#endif

  do {
#if 0
#ifdef HAVE_PWRITE
    if (-1 == (len=libc_pwrite(__fd, buf, __len, __off)))
      return -1;
    __off += len;
#else
    if (-1 == (len=libc_write(__fd, buf, __len)))
      return -1;
#endif
#endif

    buf += len;
    __len -= len;
  } while (__len > 0);

  return 0;
}


/****************************************************************************/
/*! Admits the supplied range of pages into RAM, reading any necessary pages
 *  from disk. */
/****************************************************************************/
static inline ssize_t
__mmu_admit2__(struct mmu * const __mmu, struct ate * const __ate,
               size_t const __beg, size_t const __end, int const __prot)
{
  int fd, ret;
  size_t i, numrd, page_size;
  ssize_t ii;
  void * ghost;

  numrd     = 0;
  page_size = __mmu->page_size;

  /* allocate ghost pages */
  ghost = (void*)mmap(NULL, (__end-__beg)*page_size, PROT_WRITE, MMAP_FLAGS, -1, 0);
  if (MAP_FAILED == ghost)
    goto CLEANUP;

#if 0
  /* open the file for reading */
  fd = libc_open(ate->file, O_RDONLY);
#endif
  if (NULL == __ate) {}
  if (-1 == fd)
    return -1;

  /* loop through range, updating page flags and reading from disk in bulk
   * whenever possible */
  for (ii=-1,i=__beg; i<=__end; ++i) {
    if (i != __end)
      assert(__mmu_get_flag__(__mmu, i, MMU_ALLOC));

    if (i != __end &&\
        !__mmu_get_flag__(__mmu, i, MMU_RSDNT) &&\
        !__mmu_get_flag__(__mmu, i, MMU_DIRTY) &&\
        !__mmu_get_flag__(__mmu, i, MMU_ZFILL))
    {
      if (-1 == ii)
        ii = i;
    }
    else if (-1 != ii) {
      /* read from disk */
      ret = __mmu_read__(fd,\
        (void*)((uintptr_t)ghost+((ii-__beg)*page_size)), (i-ii)*page_size,\
        (ii*page_size)-(uintptr_t)__ate->base);
      if (-1 == ret)
        goto CLEANUP;

      numrd += (i-ii);

      ii = -1;
    }
  }

  /* close file */
  ret = close(fd);
  if (-1 == ret)
    goto CLEANUP;

  /* update protection of ghost pages */
  ret = mprotect(ghost, (__end-__beg)*page_size, __prot);
  if (-1 == ret)
    goto CLEANUP;

  /* remap ghost pages into persistent pages */
  ghost = mremap(ghost, (__end-__beg)*page_size, (__end-__beg)*page_size,\
    MREMAP_MAYMOVE|MREMAP_FIXED, (void*)(__beg*page_size));
  if (MAP_FAILED == ghost)
    goto CLEANUP;

  return numrd;

  CLEANUP:
  if (-1 != fd)
    (void)close(fd);
  return -1;
}


/****************************************************************************/
/*! Internal: Admits the supplied range of pages into RAM, reading any
 *  necessary pages from disk. */
/****************************************************************************/
static inline ssize_t
__mmu_admit__(struct mmu * const __mmu, struct ate * const __ate,
              size_t const __beg, size_t const __end, int const __prot,
              int const __admit)
{
  int ret, flag;
  size_t i, page_size;
  ssize_t numrd;

  numrd     = 0;
  page_size = __mmu->page_size;

  if (PROT_READ == __prot)
    flag = MMU_RSDNT;
  else if ((PROT_READ|PROT_WRITE) == __prot)
    flag = MMU_DIRTY|MMU_RSDNT|MMU_ZFILL;
  else
    return -1;

  if (MMU_ADMIT == __admit) {
    numrd = __mmu_admit2__(__mmu, __ate, __beg, __end, __prot);
    if (-1 == numrd)
      return -1;
  }

  /* loop through range, updating page flags */
  for (i=__beg; i<__end; ++i) {
    assert(__mmu_get_flag__(__mmu, i, MMU_ALLOC));

    if (!__mmu_get_flag__(__mmu, i, MMU_RSDNT))
      __ate->l_pages++;

    __mmu_set_flag__(__mmu, i, flag);
  }

  if (MMU_ADMIT != __admit) {
    /* update protection of persistent pages */
    ret = mprotect((void*)(__beg*page_size), (__end-__beg)*page_size, __prot);
    if (-1 == ret)
      return -1;
  }

  return numrd;
}


/****************************************************************************/
/*! Internal: Flushes the supplied range of pages, writing any dirty pages to
 *  disk. */
/****************************************************************************/
static inline ssize_t
__mmu_flush__(struct mmu * const __mmu, struct ate * const __ate,
              size_t const __beg, size_t const __end)
{
  int fd, ret;
  size_t i, numwr, page_size;
  ssize_t ii;

  numwr     = 0;
  page_size = __mmu->page_size;

#if 0
  /* open the file for reading */
  fd = libc_open(__ate->file, O_WRONLY);
#endif
  if (NULL != __ate) {}
  if (-1 == fd)
    return -1;

  /* loop through range, writing to disk in bulk whenever possible */
  for (ii=-1,i=__beg; i<=__end; ++i) {
    if (i != __end && __mmu_get_flag__(__mmu, i, MMU_DIRTY)) {
      assert(__mmu_get_flag__(__mmu, i, MMU_ZFILL|MMU_ALLOC));

      __mmu_unset_flag__(__mmu, i, MMU_ZFILL);

      if (-1 == ii)
        ii = i;
    }
    else if (-1 != ii) {
      /* write to disk */
      ret = __mmu_write__(fd, (void*)(ii*page_size), (i-ii)*page_size,\
        (ii*page_size)-(uintptr_t)__ate->base);
      if (-1 == ret)
        goto CLEANUP;

      numwr += (i-ii);

      ii = -1;
    }
  }

  /* close file */
  ret = close(fd);
  if (-1 == ret)
    return -1;

  return numwr;

  CLEANUP:
  if (-1 != fd)
    (void)close(fd);
  return -1;
}


/****************************************************************************/
/*! Internal: Evicts the supplied range of pages from RAM, writing any dirty
 *  pages to disk. */
/****************************************************************************/
static inline ssize_t
__mmu_evict__(struct mmu * const __mmu, struct ate * const __ate,
              size_t const __beg, size_t const __end, int const __flush)
{
  int ret;
  size_t i, page_size;
  ssize_t numwr;

  numwr     = 0;
  page_size = __mmu->page_size;

  /* shortcut by checking to see if no pages are currently loaded */
  /* TODO: if we track the number of dirty pages, then this can do a better
   * job of short-cutting */
  if (0 == __ate->l_pages)
    return 0;

  if (MMU_FLUSH == __flush) {
    numwr = __mmu_flush__(__mmu, __ate, __beg, __end);
    if (-1 == numwr)
      return -1;
  }

  /* loop through range, update page flags */
  for (i=__beg; i<__end; ++i) {
    assert(__mmu_get_flag__(__mmu, i, MMU_ALLOC));

    if (__mmu_get_flag__(__mmu, i, MMU_RSDNT)) {
      assert(__ate->l_pages > 0);
      __ate->l_pages--;
    }

    __mmu_unset_flag__(__mmu, i, MMU_RSDNT|MMU_DIRTY);
  }

  /* unlock pages from RAM */
  ret = munlock((void*)(__beg*page_size), (__end-__beg)*page_size);
  if (-1 == ret)
    return -1;

  /* update protection of pages to none */
  ret = mprotect((void*)(__beg*page_size), (__end-__beg)*page_size,\
    PROT_NONE);
  if (-1 == ret)
    return -1;

  /* advise kernel to release resources associated with pages */
  ret = madvise((void*)(__beg*page_size), (__end-__beg)*page_size,\
    MADV_DONTNEED);
  if (-1 == ret)
    return -1;

  return numwr;
}


/****************************************************************************/
/*! Update memory protection for a range of pages. */
/****************************************************************************/
static inline int
__mmu_protect__(struct mmu * const __mmu, void * const __addr,
                size_t const __num, int const __prot, int const __opt)
{
  size_t beg, end;
  ssize_t retval=-1;
  struct ate * ate;

  /* __addr must be multiples of page_size */
  if (0 != ((uintptr_t)__addr & (__mmu->page_size-1)))
    return -1;

  /* get allocation table entry */
  ate = __mmu_lock_ate__(__mmu, __addr);
  if (NULL == ate)
    return -1;

  /* __addr and __num must be valid w.r.t. ate */
  assert(ate->base <= __addr);
  assert(__num*__mmu->page_size <= ate->len);
  assert(__addr <= (void*)((uintptr_t)ate->base+ate->len-(__num*__mmu->page_size)));

  /* get range of pages */
  beg = __mmu_get_idx__(__mmu, __addr);
  end = beg+__num;

  /* handle protection accordingly */
  switch (__prot) {
    case PROT_NONE:
    retval = __mmu_evict__(__mmu, ate, beg, end, __opt);
    break;

    case PROT_READ:
    case PROT_READ|PROT_WRITE:
    retval = __mmu_admit__(__mmu, ate, beg, end, __prot, __opt);
    break;

    default:
    break;
  }

  if (0 == __mmu_unlock_ate__(__mmu, ate))
    return retval;
  else
    return -1;
}


#endif
