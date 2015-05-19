#define _GNU_SOURCE


#include <fcntl.h>     /* O_RDWR, O_CREAT, O_EXCL */
#include <malloc.h>    /* struct mallinfo */
#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t */
#include <string.h>    /* memcpy */
#include <sys/mman.h>  /* mmap, munmap, madvise, mprotect */
#include <sys/stat.h>  /* S_IRUSR, S_IWUSR */
#include <sys/types.h> /* ssize_t, truncate */
#include <unistd.h>    /* sysconf, truncate */
#include "config.h"
#include "mmu.h"
#include "vmm.h"


/****************************************************************************/
/* Function prototypes for hooks. */
/****************************************************************************/
extern int     libc_open(char const *, int, ...);
extern ssize_t libc_read(int const, void * const, size_t const);
extern ssize_t libc_write(int const, void const * const, size_t const);
extern int     libc_mlock(void const * const, size_t const);
extern int     libc_munlock(void const * const, size_t const);


#define PAGE_SIZE (1<<14)
#define FSTEM     "/tmp/"
#define OPTS      0


static int init=0;
static struct vmm vmm;
#ifdef USE_PTHREAD
static pthread_mutex_t init_lock=PTHREAD_MUTEX_INITIALIZER;
#endif


/****************************************************************************/
/* Converts pages to system pages. */
/****************************************************************************/
#define __ooc_to_sys__(__N_PAGES)\
  ((__N_PAGES)*vmm.page_size/sysconf(_SC_PAGESIZE))


/****************************************************************************/
/* Initialize the ooc environment. */
/****************************************************************************/
extern int
__ooc_env_init__(void)
{
  /* acquire init lock */
  if (-1 == LOCK_GET(&init_lock))
    return -1;

  /* check if init and init if necessary */
  if (0 == init && -1 == __vmm_init__(&vmm, PAGE_SIZE, FSTEM, OPTS)) {
    (void)LOCK_LET(&init_lock);
    return -1;
  }

  init = 1;

  /* release init lock */
  if (-1 == LOCK_LET(&init_lock))
    return -1;

  return 0;
}


/****************************************************************************/
/* Destroy the ooc environment. */
/****************************************************************************/
extern int
__ooc_env_destroy__(void)
{
  /* acquire init lock */
  if (-1 == LOCK_GET(&init_lock))
    return -1;

  /* check if init and destroy if necessary */
  if (1 == init && -1 == __vmm_destroy__(&vmm)) {
    (void)LOCK_LET(&init_lock);
    return -1;
  }

  init = 0;

  /* release init lock */
  if (-1 == LOCK_LET(&init_lock))
    return -1;

  return 0;
}


/****************************************************************************/
/*! Allocate memory via anonymous mmap. */
/****************************************************************************/
extern void *
__ooc_malloc__(size_t const __size)
{
  int ret, fd;
  size_t page_size, s_pages, n_pages, f_pages;
  uintptr_t addr;
  struct ate * ate;
  char fname[FILENAME_MAX];

  /* shortcut */
  if (0 == __size)
    return NULL;

  /* compute allocation sizes */
  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  n_pages   = 1+((__size-1)/page_size);
  f_pages   = 1+((n_pages*sizeof(uint8_t)-1)/page_size);

  /* TODO: check memory file to see if there is enough free memory to complete
   * this allocation. */

  /* allocate memory */
  addr = (uintptr_t)mmap(NULL, (s_pages+n_pages+f_pages)*page_size,
    PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_LOCKED,\
    -1, 0);
  if ((uintptr_t)MAP_FAILED == addr)
    return NULL;

  /* read-only protect application pages -- this will avoid the double SIGSEGV
   * for new allocations */
  ret = mprotect((void*)(addr+(s_pages*page_size)), n_pages*page_size,\
    PROT_READ);
  if (-1 == ret)
    return NULL;

  /* create and truncate the file to size */
  if (0 > snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    addr))
  {
    return NULL;
  }
  if (-1 == (fd=libc_open(fname, O_WRONLY|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR)))
    return NULL;
  if (-1 == ftruncate(fd, n_pages*page_size))
    return NULL;
  if (-1 == close(fd))
    return NULL;

  /* set pointer for the allocation table entry */
  ate = (struct ate*)addr;

  /* populate ate structure */
  ate->n_pages = n_pages;
  ate->l_pages = n_pages;
  ate->base    = addr+(s_pages*page_size);
  ate->flags   = (uint8_t*)(addr+((s_pages+n_pages)*page_size));

  /* initialize ate lock */
  ret = LOCK_INIT(&(ate->lock));
  if (-1 == ret)
    return NULL;

  /* insert ate into mmu */
  ret = __mmu_insert_ate__(&(vmm.mmu), ate);
  if (-1 == ret)
    return NULL;

  /* track number of syspages currently loaded, currently allocated, and high
   * water mark number of syspages */
  __vmm_track__(curpages, __ooc_to_sys__(s_pages+n_pages+f_pages));
  __vmm_track__(numpages, __ooc_to_sys__(s_pages+n_pages+f_pages));
  __vmm_track__(maxpages, vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);

  return (void*)ate->base;
}


/****************************************************************************/
/*! Frees the memory associated with an anonymous mmap. */
/****************************************************************************/
extern int
__ooc_free__(void * const __ptr)
{
  int ret;
  size_t page_size, s_pages, n_pages, f_pages, l_pages;
  struct ate * ate;
  char fname[FILENAME_MAX];

  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  ate       = (struct ate*)((uintptr_t)__ptr-(s_pages*page_size));
  n_pages   = ate->n_pages;
  f_pages   = 1+((n_pages*sizeof(uint8_t)-1)/page_size);
  l_pages   = ate->l_pages;

  /* remove the file */
  if (0 > snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    (uintptr_t)ate))
  {
    return -1;
  }
  ret = unlink(fname);
  if (-1 == ret)
    return -1;

  /* invalidate ate */
  ret = __mmu_invalidate_ate__(&(vmm.mmu), ate);
  if (-1 == ret)
    return -1;

  /* destory ate lock */
  ret = LOCK_FREE(&(ate->lock));
  if (-1 == ret)
    return -1;

  /* free resources */
  ret = munmap((void*)ate, (s_pages+n_pages+f_pages)*page_size);
  if (-1 == ret)
    return -1;

  /* TODO: update memory file */

  /* track number of syspages currently loaded and allocated */
  __vmm_track__(curpages, -__ooc_to_sys__(s_pages+l_pages+f_pages));
  __vmm_track__(numpages, -__ooc_to_sys__(s_pages+n_pages+f_pages));

  return 0;
}


/****************************************************************************/
/*! Allocate memory via anonymous mmap. */
/****************************************************************************/
extern void *
__ooc_realloc__(void * const __ptr, size_t const __size)
{
  int ret;
  size_t i, page_size, s_pages, on_pages, of_pages, nn_pages, nf_pages;
  uintptr_t oaddr, naddr;
  uint8_t * oflags;
  struct ate * ate;
  char ofname[FILENAME_MAX], nfname[FILENAME_MAX];

  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  ate       = (struct ate*)((uintptr_t)__ptr-(s_pages*page_size));
  oaddr     = ate->base-(s_pages*page_size);
  oflags    = ate->flags;
  on_pages  = ate->n_pages;
  of_pages  = 1+((on_pages*sizeof(uint8_t)-1)/page_size);
  nn_pages  = 1+((__size-1)/page_size);
  nf_pages  = 1+((nn_pages*sizeof(uint8_t)-1)/page_size);

  if (nn_pages <= on_pages) {
    /* unlock the unused pages from RAM */
    ret = libc_munlock((void*)(oaddr+((s_pages+nn_pages)*page_size)),\
      (on_pages-nn_pages)*page_size);
    if (-1 == ret)
      return NULL;
    /* advise kernel to release resources of the unused pages */
    ret = madvise((void*)(oaddr+((s_pages+nn_pages)*page_size)),\
      (on_pages-nn_pages)*page_size, MADV_DONTNEED);
    if (-1 == ret)
      return NULL;

    /* TODO: should copy flags to then end of the smaller allocation, then
     * unmap the unused pages from the end of the allocation. */

    /* update page counts for the ate */
    ate->n_pages = nn_pages;
    for (i=nn_pages; i<on_pages; ++i) {
      if (MMU_RSDNT == (oflags[i]&MMU_RSDNT))
        ate->l_pages--;
    }

    /* TODO: what to do about vmm.numpages, since technically, the unused
     * pages are still allocated. furthermore, they will never be unmapped,
     * until the application exists, this is a problem. possible solution:
     * keeps a field in the ate struct which is the allocation size, then
     * munmap using this when the ate is freed. */

    /* track number of syspages currently loaded and allocated */
    __vmm_track__(curpages,\
      -__ooc_to_sys__((on_pages+of_pages)-(nn_pages+nf_pages)));
    //__vmm_track__(numpages, -__ooc_to_sys__(s_pages+n_pages+f_pages));
  }
  else {
    /* TODO: check memory file to see if there is enough free memory to complete
     * this allocation. */

    /* allocate new memory */
    naddr = (uintptr_t)mmap(NULL, (s_pages+nn_pages+nf_pages)*page_size,
      PROT_READ|PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_LOCKED, -1, 0);
    if ((uintptr_t)MAP_FAILED == naddr)
      return NULL;

    /* give old memory read permission for copy opeation */
    ret = mprotect((void*)(oaddr+(s_pages*page_size)), on_pages*page_size,\
      PROT_READ);
    if (-1 == ret)
      return NULL;

    /* copy old memory into new memory */
    memcpy((void*)(naddr+(s_pages*page_size)),\
      (void*)(oaddr+(s_pages*page_size)), on_pages*page_size);

    /* read-only protect application pages -- this will avoid the double
     * SIGSEGV for new allocations */
    ret = mprotect((void*)(naddr+(s_pages*page_size)), nn_pages*page_size,\
      PROT_READ);
    if (-1 == ret)
      return NULL;

    /* move old file to new file and trucate to size */
    if (0 > snprintf(ofname, FILENAME_MAX, "%s%d-%zx", vmm.fstem,\
      (int)getpid(), oaddr))
    {
      return NULL;
    }
    if (0 > snprintf(nfname, FILENAME_MAX, "%s%d-%zx", vmm.fstem,\
      (int)getpid(), naddr))
    {
      return NULL;
    }
    if (-1 == rename(ofname, nfname))
      return NULL;
    if (-1 == truncate(nfname, nn_pages*page_size))
      return NULL;

    /* set pointer for the allocation table entry */
    ate = (struct ate*)naddr;

    /* populate ate structure */
    ate->n_pages = nn_pages;
    ate->l_pages = nn_pages;
    ate->base    = naddr+(s_pages*page_size);
    ate->flags   = (uint8_t*)(naddr+((s_pages+nn_pages)*page_size));

    for (i=0; i<on_pages; ++i) {
      if (MMU_ZFILL == (ate->flags[i]&MMU_ZFILL))
        ate->flags[i] |= MMU_ZFILL;
    }

    /* initialize ate lock */
    ret = LOCK_INIT(&(ate->lock));
    if (-1 == ret)
      return NULL;

    /* free old allocation */
    ret = __ooc_free__((void*)(oaddr+(s_pages*page_size)));
    if (-1 == ret)
      return NULL;

    /* track number of syspages currently loaded, currently allocated, and
     * high water mark number of syspages */
    __vmm_track__(curpages, __ooc_to_sys__(s_pages+nn_pages+nf_pages));
    __vmm_track__(numpages, __ooc_to_sys__(s_pages+nn_pages+nf_pages));
    __vmm_track__(maxpages, vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);
  }

  return (void*)ate->base;
}
