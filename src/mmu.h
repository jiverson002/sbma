#ifndef __MMU_H__
#define __MMU_H__ 1

#ifdef USE_PTHREAD
# include <pthread.h>   /* pthread library */
# define INIT_LOCK(__LOCK) pthread_mutex_init(__LOCK, NULL)
# define FREE_LOCK(__LOCK) pthread_mutex_destroy(__LOCK)
# define GET_LOCK(__LOCK)  pthread_mutex_lock(__LOCK)
# define LET_LOCK(__LOCK)  pthread_mutex_unlock(__LOCK)
#else
# define LOCK_INIT(LOCK) 0
# define LOCK_FREE(LOCK) 0
# define LOCK_GET(LOCK)  0
# define LOCK_LET(LOCK)  0
#endif

struct mmu
{
  size_t  page_size;      /*!< mmu page size */
  size_t  min_alloc_size; /*!< mmu minimum allocation size */
  char *  page_table;     /*!< mmu page table */
  void ** alloc_table;    /*!< mmu allocation table */
#ifdef USE_PTHREAD
  pthread_mutex_t lock;   /*!< mutex guarding struct */
#endif
};

/*
 * Memory management unit page status code bits:
 *
 *   bit 0 ==    0: page not allocated  1: page is allocated
 *   bit 1 ==    0: zero fill allowed   1: page must be filled from disk
 *   bit 2 ==    0: page is resident    1: page is not resident
 *   bit 3 ==    0: page is unmodified  1: page is dirty
 */
enum mmu_status_code
{
  MMU_ALLOC = 1 << 0,
  MMU_ZFILL = 1 << 1,
  MMU_RSDNT = 1 << 2,
  MMU_DIRTY = 1 << 3
};

#define MMU_INIT(__MMU, __PAGE_SIZE, __MIN_ALLOC_SIZE)\
(\
  assert(0 == ((__PAGE_SIZE)&1)),              /* must be a multiple of 2 */\
  assert(0 == ((__MIN_ALLOC_SIZE)&1)),         /* ... */\
  assert((__MIN_ALLOC_SIZE) >= (__PAGE_SIZE)),\
\
  /* initialize variables */\
  (__MMU)->page_table  = MAP_FAILED,\
  (__MMU)->alloc_table = MAP_FAILED,\
\
  /* populate mmu variables */\
  (__MMU)->page_size   = __page_size,\
  (__MMU)->page_table  = (char*)mmap(NULL, MMU_MAX_MEM/(__PAGE_SIZE),\
     MMAP_PROT, MMAP_FLAGS, -1, 0),\
  (__MMU)->alloc_table = (void**)mmap(NULL, MMU_MAX_MEM/(__MIN_ALLOC_SIZE),\
    MMAP_PROT, MMAP_FLAGS, -1, 0),\
/* if */\
  0 == LOCK_INIT(&((__MMU)->lock)) &&\
    MAP_FAILED != (__MMU)->page_table &&\
    MAP_FAILED != (__MMU)->alloc_table\
/* then */\
    ?\
      0\
/* else */\
    :\
      ((MAP_FAILED == (__MMU)->page_table) ||\
        0 == munmap((__MMU)->page_table, MMU_MAX_MEM/__PAGE_SIZE)) &&\
      ((MAP_FAILED == (__MMU)->alloc_table) ||\
        0 == munmap((__MMU)->alloc_table, MMU_MAX_MEM/__MIN_ALLOC_SIZE)) &&\
      0 == LOCK_FREE(&((__MMU)->lock))\
        ?\
          -1\
        :\
          -2\
)

#define MMU_DESTROY(__MMU)\
(\
  ((MAP_FAILED == (__MMU)->page_table) ||\
    0 == munmap((__MMU)->page_table, MMU_MAX_MEM/(__MMU)->page_size)) &&\
  ((MAP_FAILED == (__MMU)->alloc_table) ||\
    0 == munmap((__MMU)->alloc_table, MMU_MAX_MEM/(__MMU)->min_alloc_size)) &&\
  0 == LOCK_FREE(&((__MMU)->lock))\
    ?\
      0\
    :\
      -1\
)

#define MMU_PAGE_ID(__MMU, __ADDR)\
  (((uintptr_t)(__ADDR)-MMU_MIN_ADDR)/(__MMU)->page_size)

#define MMU_PAGE_REF(__MMU, __ADDR)\
  ((__MMU)->page_table[MMU_PAGE_ID(__ADDR) >> 1])

#define MMU_PAGE_GET_STAT(__MMU, __ADDR)\
  (0 == MMU_PAGE_ID(__ADDR) & 1)\
    ? MMU_PAGE_REF(__MMU, __ADDR) >> 4\
    : MMU_PAGE_REF(__MMU, __ADDR) & 0x0F\
  )

#define MMU_STAT_GET_ALLOC(__STAT) (1 == ((__STAT) & MMU_ALLOC))
#define MMU_STAT_GET_ZFILL(__STAT) (0 == ((__STAT) & MMU_ZFILL))
#define MMU_STAT_GET_RSDNT(__STAT) (0 == ((__STAT) & MMU_RSDNT))
#define MMU_STAT_GET_DIRTY(__STAT) (1 == ((__STAT) & MMU_DIRTY))

#define MMU_PAGE_SET_STAT(__MMU, __ADDR, __STAT)\
  (0 == MMU_PAGE_ID(__ADDR) & 1)\
    ? ((__MMU)->page_table[MMU_PAGE_ID(__ADDR) >> 1]) =\
      (((__MMU)->page_table[MMU_PAGE_ID(__ADDR) >> 1]) & 0x0F) | (__STAT) << 4)\
    : ((__MMU)->page_table[MMU_PAGE_ID(__ADDR) >> 1]) =\
      (((__MMU)->page_table[MMU_PAGE_ID(__ADDR) >> 1]) & 0xF0) | (__STAT))\
  )

#define MMU_STAT_SET_ALLOC(__STAT) ((__STAT) | MMU_ALLOC)
#define MMU_STAT_SET_ZFILL(__STAT) ((__STAT) & ~MMU_ZFILL)
#define MMU_STAT_SET_RSDNT(__STAT) ((__STAT) & ~MMU_RSDNT)
#define MMU_STAT_SET_DIRTY(__STAT) ((__STAT) | MMU_DIRTY)

#define MMU_STAT_UNSET_ALLOC(__STAT) ((__STAT) & ~MMU_ALLOC)
#define MMU_STAT_UNSET_ZFILL(__STAT) ((__STAT) | MMU_ZFILL)
#define MMU_STAT_UNSET_RSDNT(__STAT) ((__STAT) | MMU_RSDNT)
#define MMU_STAT_UNSET_DIRTY(__STAT) ((__STAT) & ~MMU_DIRTY)

#endif
