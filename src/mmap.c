#define _GNU_SOURCE


#include <sys/mman.h>

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
/* Ensure that the vmm has been initialized. */
/****************************************************************************/
static inline int
__ooc_init_check__(void)
{
  /* acquire init lock */
  if (-1 == LOCK_GET(&init_lock))
    return -1;

  /* check if init and init if necessary */
  if (0 == init && -1 == __vmm_init__(&vmm, PAGE_SIZE, FSTEM))
    return -1;

  /* release init lock */
  if (-1 == LOCK_LET(&init_lock))
    return -1;
}


/****************************************************************************/
/*! Allocate memory via anonymous mmap. */
/****************************************************************************/
static inline void *
__ooc_mmap__(size_t const __len)
{
  int ret, fd;
  size_t page_size, s_pages, n_pages, f_pages;
  uintptr_t addr;
  struct ate * ate;
  char fname[FILENAME_MAX];

  if (-1 == __ooc_init_check__())
    return NULL;

  /* shortcut */
  if (0 == __len)
    return NULL;

  /* compute allocation sizes */
  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  n_pages   = 1+((__len-1)/page_size);
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
static inline int
__ooc_munmap__(void * const __addr)
{
  int ret;
  size_t page_size, s_pages, n_pages, f_pages, l_pages;
  struct ate * ate;
  char fname[FILENAME_MAX];

  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  ate       = (struct ate*)((uintptr_t)__addr-(s_pages*page_size));
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
}
