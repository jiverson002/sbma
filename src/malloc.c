#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif


#ifdef NDEBUG
# undef NDEBUG
#endif


#include <fcntl.h>     /* O_RDWR, O_CREAT, O_EXCL */
#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t */
#include <stdio.h>     /* FILENAME_MAX */
#include <string.h>    /* memcpy */
#include <sys/mman.h>  /* mmap, mremap, munmap, madvise, mprotect */
#include <sys/stat.h>  /* S_IRUSR, S_IWUSR */
#include <sys/types.h> /* truncate */
#include <unistd.h>    /* truncate */
#include "config.h"
#include "ipc.h"
#include "mmu.h"
#include "vmm.h"


static int init=0;
#ifdef USE_PTHREAD
static pthread_mutex_t init_lock=PTHREAD_MUTEX_INITIALIZER;
#endif


/****************************************************************************/
/*! The single instance of vmm per process. */
/****************************************************************************/
struct vmm vmm;


/****************************************************************************/
/*! Initialize the ooc environment. */
/****************************************************************************/
extern int
__ooc_init__(char const * const __fstem, size_t const __page_size,
             int const __n_procs, size_t const __max_mem, int const __opts)
{
  /* acquire init lock */
  if (-1 == LOCK_GET(&init_lock))
    return -1;

  /* check if init and init if necessary */
  if (0 == init && -1 == __vmm_init__(&vmm, __page_size, __fstem, __n_procs,
    __max_mem, __opts))
  {
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
/*! Destroy the ooc environment. */
/****************************************************************************/
extern int
__ooc_destroy__(void)
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

  /* check memory file to see if there is enough free memory to complete this
   * allocation. */
  if (VMM_LZYWR == (vmm.opts&VMM_LZYWR)) {
    ret = __ipc_madmit__(&(vmm.ipc), __vmm_to_sys__(s_pages+n_pages+f_pages));
    if (-1 == ret)
      return NULL;
  }

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
  /*if (-1 == ftruncate(fd, n_pages*page_size))
    return NULL;*/
  if (-1 == libc_close(fd))
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
  __vmm_track__(curpages, __vmm_to_sys__(s_pages+n_pages+f_pages));
  __vmm_track__(numpages, __vmm_to_sys__(s_pages+n_pages+f_pages));
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

  /* update memory file */
  if (VMM_LZYWR == (vmm.opts&VMM_LZYWR)) {
    ret = __ipc_mevict__(&(vmm.ipc),\
      -__vmm_to_sys__(s_pages+l_pages+f_pages));
    if (-1 == ret)
      return -1;
  }

  /* track number of syspages currently loaded and allocated */
  __vmm_track__(curpages, -__vmm_to_sys__(s_pages+l_pages+f_pages));
  __vmm_track__(numpages, -__vmm_to_sys__(s_pages+n_pages+f_pages));

  return 0;
}


/****************************************************************************/
/*! Re-allocate memory via anonymous mmap. */
/****************************************************************************/
extern void *
__ooc_realloc__(void * const __ptr, size_t const __size)
{
  int ret;
  size_t i, page_size, s_pages, on_pages, of_pages, ol_pages;
  size_t nn_pages, nf_pages;
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
  ol_pages  = ate->l_pages;
  nn_pages  = 1+((__size-1)/page_size);
  nf_pages  = 1+((nn_pages*sizeof(uint8_t)-1)/page_size);

  if (nn_pages == on_pages) {
    /* do nothing */
  }
  else if (nn_pages < on_pages) {
    /* resize allocation */
    /*naddr = (uintptr_t)mremap((void*)oaddr,\
      (s_pages+on_pages+of_pages)*page_size,\
      (s_pages+nn_pages+nf_pages)*page_size, MREMAP_MAYMOVE);
    if ((uintptr_t)MAP_FAILED == naddr)
      return NULL;*/

    /* adjust l_pages for the pages which will be unmapped */
    ate->n_pages = nn_pages;
    for (i=nn_pages; i<on_pages; ++i) {
      if (MMU_RSDNT != (oflags[i]&MMU_RSDNT))
        ate->l_pages--;
    }

    /* update protection for new page flags area of allocation */
    ret = mprotect((void*)(oaddr+((s_pages+nn_pages)*page_size)),\
      nf_pages*page_size, PROT_READ|PROT_WRITE);
    if (-1 == ret)
      return NULL;

    /* lock new page flags area of allocation into RAM */
    ret = libc_mlock((void*)(oaddr+((s_pages+nn_pages)*page_size)),\
      nf_pages*page_size);
    if (-1 == ret)
      return NULL;

    /* copy page flags to new location */
    memmove((void*)(oaddr+((s_pages+nn_pages)*page_size)),\
      (void*)(oaddr+((s_pages+on_pages)*page_size)), nf_pages*page_size);

    /* unmap unused section of memory */
    ret = munmap((void*)(oaddr+((s_pages+nn_pages+nf_pages)*page_size)),\
      ((on_pages-nn_pages)+(of_pages-nf_pages))*page_size);
    if (-1 == ret)
      return NULL;

    /* update memory file */
    if (VMM_LZYWR == (vmm.opts&VMM_LZYWR)) {
      ret = __ipc_mevict__(&(vmm.ipc),\
        -__vmm_to_sys__((on_pages-nn_pages)+(of_pages-nf_pages)));
      if (-1 == ret)
        return NULL;
    }

    /* track number of syspages currently loaded and allocated */
    __vmm_track__(curpages,\
      -__vmm_to_sys__((ol_pages-ate->l_pages)+(of_pages-nf_pages)));
    __vmm_track__(numpages,\
      -__vmm_to_sys__((on_pages-nn_pages)+(of_pages-nf_pages)));
  }
  else {
    /* check memory file to see if there is enough free memory to complete
     * this allocation. */
    if (VMM_LZYWR == (vmm.opts&VMM_LZYWR)) {
      ret = __ipc_madmit__(&(vmm.ipc),\
        __vmm_to_sys__((nn_pages-on_pages)+(nf_pages+of_pages)));
      if (-1 == ret)
        return NULL;
    }

    /* resize allocation */
    naddr = (uintptr_t)mremap((void*)oaddr,\
      (s_pages+on_pages+of_pages)*page_size,\
      (s_pages+nn_pages+nf_pages)*page_size, MREMAP_MAYMOVE);
    if ((uintptr_t)MAP_FAILED == naddr)
      return NULL;

    /* copy page flags to new location */
    memmove((void*)(naddr+((s_pages+nn_pages)*page_size)),\
      (void*)(naddr+((s_pages+on_pages)*page_size)), of_pages*page_size);

    /* grant read-only permission to extended area of application memory */
    ret = mprotect((void*)(naddr+((s_pages+on_pages)*page_size)),\
      (nn_pages-on_pages)*page_size, PROT_READ);
    if (-1 == ret)
      return NULL;

    /* lock new area of allocation into RAM */
    ret = libc_mlock((void*)(naddr+(s_pages+on_pages)*page_size),\
      ((nn_pages-on_pages)+nf_pages)*page_size);
    if (-1 == ret)
      return NULL;

    if (0 > snprintf(nfname, FILENAME_MAX, "%s%d-%zx", vmm.fstem,\
      (int)getpid(), naddr))
    {
      return NULL;
    }
    if (oaddr != naddr) {
      /* move old file to new file and trucate to size */
      if (0 > snprintf(ofname, FILENAME_MAX, "%s%d-%zx", vmm.fstem,\
        (int)getpid(), oaddr))
      {
        return NULL;
      }
      if (-1 == rename(ofname, nfname))
        return NULL;
    }
    if (-1 == truncate(nfname, nn_pages*page_size))
      return NULL;

    /* set pointer for the allocation table entry */
    ate = (struct ate*)naddr;

    /* populate ate structure */
    ate->n_pages = nn_pages;
    ate->l_pages =\
      ol_pages+((nn_pages-on_pages)+(nf_pages-of_pages))*page_size;
    ate->base    = naddr+(s_pages*page_size);
    ate->flags   = (uint8_t*)(naddr+((s_pages+nn_pages)*page_size));

    /* track number of syspages currently loaded, currently allocated, and
     * high water mark number of syspages */
    __vmm_track__(curpages,\
      __vmm_to_sys__((nn_pages-on_pages)+(nf_pages-of_pages)));
    __vmm_track__(numpages,\
      __vmm_to_sys__((nn_pages-on_pages)+(nf_pages-of_pages)));
    __vmm_track__(maxpages,\
      vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);
  }

  return (void*)ate->base;
}
