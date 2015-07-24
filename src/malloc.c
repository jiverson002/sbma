/*
Copyright (c) 2015, Jeremy Iverson
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif


#include <errno.h>     /* errno library */
#include <fcntl.h>     /* O_WRONLY, O_CREAT, O_EXCL */
#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t */
#include <stdio.h>     /* FILENAME_MAX */
#include <sys/mman.h>  /* mmap, mremap, munmap, madvise, mprotect */
#include <sys/stat.h>  /* S_IRUSR, S_IWUSR */
#include <sys/types.h> /* truncate, ftruncate */
#include <unistd.h>    /* truncate, ftruncate */
#include "common.h"
#include "ipc.h"
#include "lock.h"
#include "mmu.h"
#include "sbma.h"
#include "vmm.h"


/****************************************************************************/
/*! Allocate memory via anonymous mmap. */
/****************************************************************************/
SBMA_EXTERN void *
__sbma_malloc(size_t const __size)
{
  int ret, fd;
  size_t page_size, s_pages, n_pages, f_pages;
  uintptr_t addr;
  void * retval;
  struct ate * ate;
  char fname[FILENAME_MAX];

  /* Shortcut. */
  if (0 == __size)
    return NULL;

  /* Default return value. */
  retval = NULL;

  /* Compute allocation sizes. */
  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);      /* struct pages */
  n_pages   = 1+((__size-1)/page_size);                  /* app pages */
  f_pages   = 1+((n_pages*sizeof(uint8_t)-1)/page_size); /* flag pages */

  /* Check memory file to see if there is enough free memory to complete this
   * allocation. */
  for (;;) {
    ret = __ipc_madmit(&(vmm.ipc), VMM_TO_SYS(s_pages+n_pages+f_pages));
    if (-1 == ret)
      goto RETURN;
    else if (-2 != ret)
      break;
  }

  /* Allocate memory with read/write permission and locked into memory.
   * Since the SBMA library bypasses the OS swap space, MAP_NORESERVE is used
   * here to prevent the system for reserving swap space. */
  addr = (uintptr_t)mmap(NULL, (s_pages+n_pages+f_pages)*page_size,
    PROT_READ|PROT_WRITE, SBMA_MMAP_FLAG, -1, 0);
  if ((uintptr_t)MAP_FAILED == addr)
    goto CLEANUP1;

  /* Read-only protect application pages -- this will avoid the double SIGSEGV
   * for new allocations. */
  ret = mprotect((void*)(addr+(s_pages*page_size)), n_pages*page_size,\
    PROT_READ);
  if (-1 == ret)
    goto CLEANUP2;

  /* Create the file */
  ret = snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    addr);
  if (0 > ret)
    goto CLEANUP2;
  fd = libc_open(fname, O_WRONLY|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);
  if (-1 == fd)
    goto CLEANUP3;
  ret = close(fd);
  if (-1 == ret)
    goto CLEANUP3;
  /* Truncating file to size is unnecessary as it will be resized when writes
   * are made to it. Doing this now however, will let the application know if
   * the filesystem has room to support all allocations up to this point. */
  /*ret = truncate(fname, n_pages*page_size);
  if (-1 == ret)
    return NULL;*/

  /* Set and populate ate structure. */
  ate          = (struct ate*)addr;
  ate->n_pages = n_pages;
  ate->l_pages = n_pages;
  ate->c_pages = n_pages;
  ate->base    = addr+(s_pages*page_size);
  ate->flags   = (uint8_t*)(addr+((s_pages+n_pages)*page_size));

  /* Initialize ate lock. */
  ret = __lock_init(&(ate->lock));
  if (-1 == ret)
    goto CLEANUP3;

  /* Insert ate into mmu. */
  ret = __mmu_insert_ate(&(vmm.mmu), ate);
  if (-1 == ret)
    goto CLEANUP4;

  /**************************************************************************/
  /* Successful exit -- return pointer to appliction memory. */
  /**************************************************************************/
  retval = (void*)ate->base;
  goto RETURN;

  /**************************************************************************/
  /* Error exit -- revert changes to vmm state, release any memory, and
   * remove any files created, then return NULL. */
  /**************************************************************************/
  CLEANUP4:
  ret = __mmu_invalidate_ate(&(vmm.mmu), ate);
  ASSERT(-1 != ret);
  CLEANUP3:
  ret = close(fd);
  ASSERT(-1 != ret);
  ret = unlink(fname);
  ASSERT(-1 != ret);
  CLEANUP2:
  ret = munmap((void*)addr, (s_pages+n_pages+f_pages)*page_size);
  ASSERT(-1 != ret);
  CLEANUP1:
  for (;;) {
    ret = __ipc_mevict(&(vmm.ipc), VMM_TO_SYS(s_pages+n_pages+f_pages));
    if (-1 == ret)
      goto RETURN;
    else if (-2 != ret)
      break;
  }
  retval = NULL;

  /**************************************************************************/
  /* Return point -- make sure vmm is in valid state and return. */
  /**************************************************************************/
  RETURN:
  return retval;
}
SBMA_EXPORT(default, void *
__sbma_malloc(size_t const __size));


/****************************************************************************/
/*! Function for API consistency, adds no additional functionality. */
/****************************************************************************/
SBMA_EXTERN void *
__sbma_calloc(size_t const __num, size_t const __size)
{
  return __sbma_malloc(__num*__size);
}
SBMA_EXPORT(default, void *
__sbma_calloc(size_t const __num, size_t const __size));


/****************************************************************************/
/*! Frees the memory associated with an anonymous mmap. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_free(void * const __ptr)
{
  int ret, retval;
  size_t page_size, s_pages, n_pages, f_pages, c_pages;
  struct ate * ate;
  char fname[FILENAME_MAX];

  /* Default return value. */
  retval = 0;

  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  ate       = (struct ate*)((uintptr_t)__ptr-(s_pages*page_size));
  n_pages   = ate->n_pages;
  c_pages   = ate->c_pages;
  f_pages   = 1+((n_pages*sizeof(uint8_t)-1)/page_size);

  /* Remove the file. */
  ret = snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    (uintptr_t)ate);
  if (0 > ret)
    retval = -1;
  ret = unlink(fname);
  if (-1 == ret && ENOENT != errno)
    retval = -1;

  /* Invalidate ate. */
  ret = __mmu_invalidate_ate(&(vmm.mmu), ate);
  if (-1 == ret)
    retval = -1;

  /* Destory ate lock. */
  ret = __lock_free(&(ate->lock));
  if (-1 == ret)
    retval = -1;

  /* Free resources. */
  ret = munmap((void*)ate, (s_pages+n_pages+f_pages)*page_size);
  if (-1 == ret)
    retval = -1;

  /* Update memory file. */
  for (;;) {
    retval = __ipc_mevict(&(vmm.ipc), VMM_TO_SYS(s_pages+c_pages+f_pages));
    if (-2 != retval)
      break;
  }

  /**************************************************************************/
  /* Return point -- make sure vmm is in valid state and return. */
  /**************************************************************************/
  return retval;
}
SBMA_EXPORT(default, int
__sbma_free(void * const __ptr));


/****************************************************************************/
/*! Re-allocate memory via anonymous mmap. */
/****************************************************************************/
SBMA_EXTERN void *
__sbma_realloc(void * const __ptr, size_t const __size)
{
  int ret;
  size_t i, page_size, s_pages, on_pages, of_pages, ol_pages, oc_pages;
  size_t nn_pages, nf_pages;
  uintptr_t oaddr, naddr;
  volatile uint8_t * oflags;
  struct ate * ate;
  char ofname[FILENAME_MAX], nfname[FILENAME_MAX];

  /* TODO: Need to make sure that in case of an error, the state of vmm.ipc is
   * correct. For instance, in the 'else' case, the first step taken is to
   * call madmit, then if the calls fails, the vmm.ipc state will be
   * incorrect. So, if an error occurs, then mevict must be called to offset
   * and correct the state of vmm.ipc. */

  if (0 == __size)
    return NULL;

  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  ate       = (struct ate*)((uintptr_t)__ptr-(s_pages*page_size));
  oaddr     = ate->base-(s_pages*page_size);
  oflags    = ate->flags;
  on_pages  = ate->n_pages;
  of_pages  = 1+((on_pages*sizeof(uint8_t)-1)/page_size);
  ol_pages  = ate->l_pages;
  oc_pages  = ate->c_pages;
  nn_pages  = 1+((__size-1)/page_size);
  nf_pages  = 1+((nn_pages*sizeof(uint8_t)-1)/page_size);

  if (nn_pages == on_pages) {
    /* do nothing */
  }
  else if (nn_pages < on_pages) {
    /* adjust c_pages for the pages which will be unmapped */
    ate->n_pages = nn_pages;
    for (i=nn_pages; i<on_pages; ++i) {
      if (MMU_RSDNT != (oflags[i]&MMU_RSDNT)) {
        ate->l_pages--;
        ate->c_pages--;
      }
    }

    /* update protection for new page flags area of allocation */
    ret = mprotect((void*)(oaddr+((s_pages+nn_pages)*page_size)),\
      nf_pages*page_size, PROT_READ|PROT_WRITE);
    if (-1 == ret)
      return NULL;

#if MAP_LOCKED == (SBMA_MMAP_FLAG&MAP_LOCKED)
    /* lock new page flags area of allocation into RAM */
    ret = libc_mlock((void*)(oaddr+((s_pages+nn_pages)*page_size)),\
      nf_pages*page_size);
    if (-1 == ret)
      return NULL;
#endif

    /* copy page flags to new location */
    libc_memmove((void*)(oaddr+((s_pages+nn_pages)*page_size)),\
      (void*)(oaddr+((s_pages+on_pages)*page_size)), nf_pages*page_size);

    /* unmap unused section of memory */
    ret = munmap((void*)(oaddr+((s_pages+nn_pages+nf_pages)*page_size)),\
      ((on_pages-nn_pages)+(of_pages-nf_pages))*page_size);
    if (-1 == ret)
      return NULL;

    /* update memory file */
    for (;;) {
      ret = __ipc_mevict(&(vmm.ipc),\
        VMM_TO_SYS((oc_pages-ate->c_pages)+(of_pages-nf_pages)));
      if (-1 == ret)
        return NULL;
      else if (-2 != ret)
        break;
    }
  }
  else {
    /* check memory file to see if there is enough free memory to complete
     * this allocation. */
    for (;;) {
      ret = __ipc_madmit(&(vmm.ipc),\
        VMM_TO_SYS((nn_pages-on_pages)+(nf_pages-of_pages)));
      if (-1 == ret)
        return NULL;
      else if (-2 != ret)
        break;
    }

    /* remove old ate from mmu */
    ret = __mmu_invalidate_ate(&(vmm.mmu), ate);
    if (-1 == ret)
      goto CLEANUP;

    /* resize allocation */
#if 1
    /* TODO: I think the reason that mremap fails so frequently is due to the
     * fact that oaddr is not seen as a single vma in the kernel, but rather
     * as several vmas, due to the use of mprotect to manage access to the
     * memory region. */
    /* Make sure the kernel sees the entire range as a single vma. */
    /*ret = mprotect((void*)oaddr, (s_pages+on_pages+of_pages)*page_size,\
      PROT_NONE);
    if (-1 == ret)
      goto CLEANUP1;*/
    naddr = (uintptr_t)mremap((void*)oaddr,\
      (s_pages+on_pages+of_pages)*page_size,\
      (s_pages+nn_pages+nf_pages)*page_size, MREMAP_MAYMOVE);
#else
    naddr = (uintptr_t)mremap((void*)oaddr,\
      (s_pages+on_pages+of_pages)*page_size,\
      (s_pages+nn_pages+nf_pages)*page_size, 0);
#endif
    if ((uintptr_t)MAP_FAILED == naddr) {
      //printf("[%5d] had to malloc(%zu)\n", (int)getpid(), __size);
      goto CLEANUP1;
    }
    //printf("[%5d] got to realloc(%zu)\n", (int)getpid(), __size);

    /* copy page flags to new location */
    libc_memmove((void*)(naddr+((s_pages+nn_pages)*page_size)),\
      (void*)(naddr+((s_pages+on_pages)*page_size)), of_pages*page_size);

    /* grant read-only permission to extended area of application memory */
    ret = mprotect((void*)(naddr+((s_pages+on_pages)*page_size)),\
      (nn_pages-on_pages)*page_size, PROT_READ);
    if (-1 == ret)
      goto CLEANUP;

#if MAP_LOCKED == (SBMA_MMAP_FLAG&MAP_LOCKED)
    /* lock new area of allocation into RAM */
    ret = libc_mlock((void*)(naddr+(s_pages+on_pages)*page_size),\
      ((nn_pages-on_pages)+nf_pages)*page_size);
    if (-1 == ret)
      goto CLEANUP;
#endif

    ret = snprintf(nfname, FILENAME_MAX, "%s%d-%zx", vmm.fstem,\
      (int)getpid(), naddr);
    if (0 > ret)
      return NULL;
    /* if the allocation has moved */
    if (oaddr != naddr) {
      /* move old file to new file and trucate to size */
      ret = snprintf(ofname, FILENAME_MAX, "%s%d-%zx", vmm.fstem,\
        (int)getpid(), oaddr);
      if (0 > ret)
        goto CLEANUP;
      ret = rename(ofname, nfname);
      if (-1 == ret)
        goto CLEANUP;

      /* set pointer for the allocation table entry */
      ate = (struct ate*)naddr;
    }
    /*ret = truncate(nfname, nn_pages*page_size);
    if (-1 == ret)
      return NULL;*/

    /* insert new ate into mmu */
    ret = __mmu_insert_ate(&(vmm.mmu), ate);
    if (-1 == ret)
      goto CLEANUP;

    /* populate ate structure */
    ate->n_pages = nn_pages;
    ate->l_pages = ol_pages+((nn_pages-on_pages)+(nf_pages-of_pages));
    ate->c_pages = oc_pages+((nn_pages-on_pages)+(nf_pages-of_pages));
    ate->base    = naddr+(s_pages*page_size);
    ate->flags   = (uint8_t*)(naddr+((s_pages+nn_pages)*page_size));

    goto DONE;

    CLEANUP1:
    /* insert old ate back into mmu */
    ret = __mmu_insert_ate(&(vmm.mmu), ate);
    ASSERT(-1 != ret);
    CLEANUP:
    for (;;) {
      ret = __ipc_mevict(&(vmm.ipc),\
        VMM_TO_SYS((nn_pages-on_pages)+(nf_pages-of_pages)));
      if (-1 == ret)
        return NULL;
      else if (-2 != ret)
        break;
    }

    return NULL;
  }

  DONE:
  return (void*)ate->base;
}
SBMA_EXPORT(default, void *
__sbma_realloc(void * const __ptr, size_t const __size));


#if 0
/****************************************************************************/
/*! Remap an address range to a new address. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_remap(void * const __nbase, void * const __obase, size_t const __size,
             size_t const __off)
{
  int ret;
  size_t i, page_size, s_pages, beg, end;
  uintptr_t oaddr, naddr;
  void * optr, * nptr;
  volatile uint8_t * oflags, * nflags;
  struct ate * oate, * nate;
  char ofname[FILENAME_MAX], nfname[FILENAME_MAX];

  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  oate      = (struct ate*)((uintptr_t)__obase-(s_pages*page_size));
  oaddr     = (uintptr_t)oate;
  oflags    = oate->flags;
  optr      = (void*)((uintptr_t)__obase+__off);
  nate      = (struct ate*)((uintptr_t)__nbase-(s_pages*page_size));
  naddr     = (uintptr_t)nate;
  nflags    = nate->flags;
  nptr      = (void*)((uintptr_t)__nbase+__off);

  ASSERT((uintptr_t)__obase == oate->base);
  ASSERT((uintptr_t)__nbase == nate->base);
  ASSERT(oate->n_pages <= nate->n_pages);

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)nptr-nate->base)/page_size;
  end = 1+(((uintptr_t)nptr+__size-nate->base-1)/page_size);

  ASSERT(0 == beg);
  ASSERT(end <= oate->n_pages);
  ASSERT(end <= nate->n_pages);

  /* load new and old memory */
  ret = __sbma_mtouch_atomic((void*)nate->base, nate->n_pages*page_size,\
    optr, __size, SBMA_ATOMIC_END);
  if (-1 == ret)
    return -1;
  ASSERT(nate->l_pages == nate->n_pages);
  ASSERT(nate->c_pages == nate->n_pages);

  /* grant read-write permission to new memory */
  ret = mprotect((void*)((uintptr_t)nptr-__off), __size+__off,\
    PROT_READ|PROT_WRITE);
  if (-1 == ret)
    return -1;

  /* copy memory */
  libc_memcpy(nptr, optr, __size);

  /* grant read-only permission to new memory */
  ret = mprotect((void*)((uintptr_t)nptr-__off), __size+__off, PROT_READ);
  if (-1 == ret)
    return -1;

  for (i=beg; i<end; ++i) {
    ASSERT(MMU_RSDNT != (nflags[i]&MMU_RSDNT));
    ASSERT(MMU_CHRGD != (nflags[i]&MMU_CHRGD));

    /* copy zfill and dirty bit from old flag for clean pages */
    if (MMU_DIRTY != (nflags[i]&MMU_DIRTY))
      nflags[i] |= (oflags[i]&(MMU_ZFILL|MMU_DIRTY));

    /* grant read-write permission to dirty pages of new memory */
    if (MMU_DIRTY == (nflags[i]&MMU_DIRTY)) {
      ret = mprotect((void*)((uintptr_t)__nbase+(i*page_size)), page_size,\
        PROT_READ|PROT_WRITE);
      if (-1 == ret)
        return -1;
    }
  }

  /* This loop addresses the trailing bytes of the new file (>= __off+__size)
   * which are necessarily overwritten when the old file is renamed to the new
   * file. The assumption here is that the number of such pages will be small,
   * else the remap in not beneficial. */
  for (i=end; i<nate->n_pages; ++i) {
    ASSERT(MMU_RSDNT != (nflags[i]&MMU_RSDNT));
    ASSERT(MMU_CHRGD != (nflags[i]&MMU_CHRGD));

    /* change non-dirty disk pages to dirty since, they were overwritten by
     * new file and grant them read-write permission */
    if (MMU_ZFILL == (nflags[i]&(MMU_DIRTY|MMU_ZFILL))) {
      nflags[i] &= ~MMU_ZFILL;
      nflags[i] |= MMU_DIRTY;

      ret = mprotect((void*)((uintptr_t)__nbase+(i*page_size)), page_size,\
        PROT_READ|PROT_WRITE);
      if (-1 == ret)
        return -1;
    }
  }

  /* move old file to new file and truncate to size. */
  ret = snprintf(nfname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    naddr);
  if (ret < 0)
    return -1;
  ret = snprintf(ofname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    oaddr);
  if (ret < 0)
    return -1;
  ret = rename(ofname, nfname);
  if (-1 == ret)
    return -1;
  /*ret = truncate(nfname, nn_pages*page_size);
  if (-1 == ret)
    return -1;*/

  return 0;
}
SBMA_EXPORT(internal, int
__sbma_remap(void * const __nbase, void * const __obase, size_t const __size,
             size_t const __off));
#else
/****************************************************************************/
/*! Remap an address range to a new LARGER address range. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_remap(void * const __nbase, void * const __obase, size_t const __size)
{
  int ret;
  size_t i, page_size, s_pages;
  void * optr, * nptr;
  volatile uint8_t * oflags, * nflags;
  struct ate * oate, * nate;
  char ofname[FILENAME_MAX], nfname[FILENAME_MAX];

  if (0 == __size)
    return -1;

  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  oate      = (struct ate*)((uintptr_t)__obase-(s_pages*page_size));
  oflags    = oate->flags;
  nate      = (struct ate*)((uintptr_t)__nbase-(s_pages*page_size));
  nflags    = nate->flags;

  ASSERT((uintptr_t)__obase == oate->base);
  ASSERT((uintptr_t)__nbase == nate->base);
  ASSERT(oate->n_pages <= nate->n_pages);

  /* Make sure that old memory is stored in file */
  ret = __sbma_mevict((void*)oate->base, oate->n_pages*page_size);
  if (-1 == ret)
    return -1;
  /* Make sure that new memory is uninitialized */
  ret = __sbma_mclear((void*)nate->base, nate->n_pages*page_size);
  if (-1 == ret)
    return -1;
  /* Make sure that new memory has no read permissions so that it will load
   * from disk any necessary pages. */
  ret = __sbma_mevict((void*)nate->base, nate->n_pages*page_size);
  if (-1 == ret)
    return -1;

  for (i=0; i<oate->n_pages; ++i) {
    ASSERT(MMU_DIRTY != (nflags[i]&MMU_DIRTY)); /* not dirty */
    ASSERT(MMU_ZFILL != (nflags[i]&MMU_ZFILL)); /* not on disk */

    /* copy zfill bit from old flag */
    nflags[i] |= (oflags[i]&MMU_ZFILL);
  }

  /* move old file to new file and truncate to size. */
  ret = snprintf(nfname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    (uintptr_t)nate);
  if (ret < 0)
    return -1;
  ret = snprintf(ofname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    (uintptr_t)oate);
  if (ret < 0)
    return -1;
  ret = rename(ofname, nfname);
  if (-1 == ret)
    return -1;
  /*ret = truncate(nfname, nn_pages*page_size);
  if (-1 == ret)
    return -1;*/

  return 0;
}
SBMA_EXPORT(internal, int
__sbma_remap(void * const __nbase, void * const __obase,
             size_t const __size));
#endif
