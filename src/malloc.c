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
  size_t i, page_size, s_pages, n_pages, f_pages;
  uintptr_t addr;
  void * retval;
  struct ate * ate;
  char fname[FILENAME_MAX];

  /* Shortcut. */
  if (0 == __size)
    return NULL;

  SBMA_STATE_CHECK();

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
#if SBMA_CHARGE_META == 1
# if SBMA_RESIDENT_DEFAULT == 1
    ret = __ipc_madmit(&(vmm.ipc), VMM_TO_SYS(s_pages+n_pages+f_pages));
# else
    ret = __ipc_madmit(&(vmm.ipc), VMM_TO_SYS(s_pages+f_pages));
# endif
#else
# if SBMA_RESIDENT_DEFAULT == 1
    ret = __ipc_madmit(&(vmm.ipc), VMM_TO_SYS(n_pages));
# else
    ret = 0;
# endif
#endif
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

#if SBMA_RESIDENT_DEFAULT == 1
  /* Read-only protect application pages -- this will avoid the double SIGSEGV
   * for new allocations. */
  ret = mprotect((void*)(addr+(s_pages*page_size)), n_pages*page_size,\
    PROT_READ);
#else
  /* Remove all protectection from application pages -- this reduces the
   * amount of memory which is admitted by default. */
  ret = mprotect((void*)(addr+(s_pages*page_size)), n_pages*page_size,\
    PROT_NONE);
#endif
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
#if SBMA_FILE_RESERVE == 1
  ret = truncate(fname, n_pages*page_size);
  if (-1 == ret)
    return NULL;
#endif

  /* Set and populate ate structure. */
  ate          = (struct ate*)addr;
  ate->n_pages = n_pages;
#if SBMA_RESIDENT_DEFAULT == 1
  ate->l_pages = n_pages;
  ate->c_pages = n_pages;
#else
  ate->l_pages = 0;
  ate->c_pages = 0;
#endif
  ate->base    = addr+(s_pages*page_size);
  ate->flags   = (uint8_t*)(addr+((s_pages+n_pages)*page_size));

#if SBMA_RESIDENT_DEFAULT == 0
  for (i=0; i<n_pages; ++i)
    ate->flags[i] |= (MMU_CHRGD|MMU_RSDNT);
#endif

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
#if SBMA_CHARGE_META == 1
# if SBMA_RESIDENT_DEFAULT == 1
    ret = __ipc_mevict(&(vmm.ipc), VMM_TO_SYS(s_pages+n_pages+f_pages));
# else
    ret = __ipc_mevict(&(vmm.ipc), VMM_TO_SYS(s_pages+f_pages));
# endif
#else
# if SBMA_RESIDENT_DEFAULT == 1
    ret = __ipc_mevict(&(vmm.ipc), VMM_TO_SYS(n_pages));
# else
    ret = 0;
# endif
#endif
    if (-1 == ret)
      goto RETURN;
    else if (-2 != ret)
      break;
  }

  /**************************************************************************/
  /* Return point -- make sure vmm is in valid state and return. */
  /**************************************************************************/
  RETURN:
  SBMA_STATE_CHECK();
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

  SBMA_STATE_CHECK();

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
#if SBMA_CHARGE_META == 1
    retval = __ipc_mevict(&(vmm.ipc), VMM_TO_SYS(s_pages+c_pages+f_pages));
#else
    retval = __ipc_mevict(&(vmm.ipc), VMM_TO_SYS(c_pages));
#endif
    if (-2 != retval)
      break;
  }

  /**************************************************************************/
  /* Return point -- make sure vmm is in valid state and return. */
  /**************************************************************************/
  SBMA_STATE_CHECK();
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
  size_t i, ifirst, page_size, s_pages, on_pages, of_pages, ol_pages, oc_pages;
  size_t nn_pages, nf_pages;
  uint8_t oflag;
  uintptr_t oaddr, naddr;
  volatile uint8_t * oflags, * nflags;
  struct ate * ate;
  char ofname[FILENAME_MAX], nfname[FILENAME_MAX];

  /* TODO: Need to make sure that in case of an error, the state of vmm.ipc is
   * correct. For instance, in the 'else' case, the first step taken is to
   * call madmit, then if the calls fails, the vmm.ipc state will be
   * incorrect. So, if an error occurs, then mevict must be called to offset
   * and correct the state of vmm.ipc. */

  if (0 == __size)
    return NULL;

  SBMA_STATE_CHECK();

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

  size_t chk_c_mem   = vmm.ipc.c_mem[vmm.ipc.id];
  size_t chk_c_pages = 0;
  struct ate * _ate;
  ret = __lock_get(&(vmm.lock));
  if (-1 == ret)
    goto CLEANUP1;
  for (_ate=vmm.mmu.a_tbl; NULL!=_ate; _ate=_ate->next) {
    ret = __lock_get(&(_ate->lock));
    ASSERT(-1 != ret);

    size_t s_pages  = 1+((sizeof(struct ate)-1)/vmm.page_size);
    size_t f_pages  = 1+((_ate->n_pages*sizeof(uint8_t)-1)/vmm.page_size);
    chk_c_pages += s_pages+_ate->c_pages+f_pages;

    ret = __lock_let(&(_ate->lock));
    ASSERT(-1 != ret);
  }
  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    goto CLEANUP1;


  if (nn_pages == on_pages) {
    /* do nothing */
  }
  else if (nn_pages < on_pages) {
    /* adjust c_pages for the pages which will be unmapped */
    ate->n_pages = nn_pages;
    for (i=nn_pages; i<on_pages; ++i) {
      if (MMU_RSDNT != (oflags[i]&MMU_RSDNT)) {
        ASSERT(ate->l_pages > 0);
        ate->l_pages--;
      }
      if (MMU_CHRGD != (oflags[i]&MMU_CHRGD)) {
        ASSERT(ate->c_pages > 0);
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
#if SBMA_CHARGE_META == 1
# if SBMA_RESIDENT_DEFAULT == 1
      ret = __ipc_madmit(&(vmm.ipc),\
        VMM_TO_SYS((nn_pages-on_pages)+(nf_pages-of_pages)));
# else
      ret = __ipc_madmit(&(vmm.ipc), VMM_TO_SYS(nf_pages-of_pages));
# endif
# if SBMA_RESIDENT_DEFAULT == 1
      ret = __ipc_madmit(&(vmm.ipc), VMM_TO_SYS((nn_pages-on_pages)));
# else
      ret = 0;
# endif
#endif
      if (-1 == ret)
        return NULL;
      else if (-2 != ret)
        break;
    }

    /* remove old ate from mmu */
    ret = __mmu_invalidate_ate(&(vmm.mmu), ate);
    if (-1 == ret)
      goto CLEANUP;

#if SBMA_MERGE_VMA == 1
    /* TODO: I think the reason that mremap fails so frequently is due to the
     * fact that oaddr is not seen as a single vma in the kernel, but rather
     * as several vmas, due to the use of mprotect to manage access to the
     * memory region. */
    /* Make sure the kernel sees the entire range as a single vma. */
    ret = mprotect((void*)oaddr, (s_pages+on_pages+of_pages)*page_size,\
      PROT_READ);
    if (-1 == ret)
      goto CLEANUP1;
#endif

    /* resize allocation */
    naddr = (uintptr_t)mremap((void*)oaddr,\
      (s_pages+on_pages+of_pages)*page_size,\
      (s_pages+nn_pages+nf_pages)*page_size, MREMAP_MAYMOVE);
    if ((uintptr_t)MAP_FAILED == naddr) {
      printf("[%5d] %s:%d %s\n", (int)getpid(), __func__, __LINE__, strerror(errno));
      goto CLEANUP1;
    }

    /* Update protection for book-keeping memory and temporarily for
     * application memory. */
#if SBMA_MERGE_VMA == 1
    ret = mprotect((void*)naddr, s_pages*page_size, PROT_READ|PROT_WRITE);
    if (-1 == ret)
      goto CLEANUP;
    ret = mprotect((void*)(naddr+(s_pages+nn_pages)*page_size),\
      nf_pages*page_size, PROT_READ|PROT_WRITE);
    if (-1 == ret)
      goto CLEANUP;
#endif

    /* copy page flags to new location */
    libc_memmove((void*)(naddr+((s_pages+nn_pages)*page_size)),\
      (void*)(naddr+((s_pages+on_pages)*page_size)), of_pages*page_size);

#if SBMA_MERGE_VMA == 1
# if SBMA_RESIDENT_DEFAULT == 1
    /* grant read-only permission application memory */
    ret = mprotect((void*)(naddr+(s_pages*page_size)), nn_pages*page_size,\
      PROT_READ);
# else
    /* grant no permission to application memory */
    ret = mprotect((void*)(naddr+(s_pages*page_size)), nn_pages*page_size,\
      PROT_NONE);
# endif
#else
# if SBMA_RESIDENT_DEFAULT == 1
    /* grant read-only permission to extended area of application memory */
    ret = mprotect((void*)(naddr+((s_pages+on_pages)*page_size)),\
      (nn_pages-on_pages)*page_size, PROT_READ);
# else
    /* grant no permission to extended area of application memory */
    ret = mprotect((void*)(naddr+((s_pages+on_pages)*page_size)),\
      (nn_pages-on_pages)*page_size, PROT_NONE);
# endif
#endif
    if (-1 == ret)
      goto CLEANUP;

#if SBMA_MERGE_VMA == 1
# if 1
    /* Update memory protection according to the existing page flags. */
    nflags = (uint8_t*)(naddr+((s_pages+nn_pages)*page_size));
    ifirst = 0;
    oflag  = nflags[0];
    for (i=0; i<=on_pages; ++i) {
      if (i == on_pages || oflag != (nflags[i]&(MMU_RSDNT|MMU_DIRTY))) {
        if (MMU_DIRTY == (oflag&MMU_DIRTY)) {
          ret = mprotect((void*)(naddr+(s_pages+ifirst)*page_size),\
            (i-ifirst)*page_size, PROT_READ|PROT_WRITE);
        }
        else if (MMU_RSDNT != (oflag&MMU_RSDNT)) {
          ret = mprotect((void*)(naddr+(s_pages+i)*page_size),\
            (i-ifirst)*page_size, PROT_READ);
        }
        if (-1 == ret)
          goto CLEANUP;

        if (i != on_pages) {
          ifirst = i;
          oflag  = nflags[i];
        }
      }
    }
# else
    nflags = (uint8_t*)(naddr+((s_pages+nn_pages)*page_size));
    for (i=0; i<on_pages; ++i) {
      if (MMU_DIRTY == (nflags[i]&MMU_DIRTY)) {
        ret = mprotect((void*)(naddr+(s_pages+i)*page_size), page_size,\
          PROT_READ|PROT_WRITE);
        if (-1 == ret)
          goto CLEANUP;
      }
      else if (MMU_RSDNT != (nflags[i]&MMU_RSDNT)) {
        ret = mprotect((void*)(naddr+(s_pages+i)*page_size), page_size,\
          PROT_READ);
        if (-1 == ret)
          goto CLEANUP;
      }
    }
# endif
#endif

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
#if SBMA_FILE_RESERVE == 1
    ret = truncate(nfname, nn_pages*page_size);
    if (-1 == ret)
      return NULL;
#endif

    /* insert new ate into mmu */
    ret = __mmu_insert_ate(&(vmm.mmu), ate);
    if (-1 == ret)
      goto CLEANUP;

    /* populate ate structure */
    ate->n_pages = nn_pages;
#if SBMA_RESIDENT_DEFAULT == 1
    ate->l_pages = ol_pages+((nn_pages-on_pages);
    ate->c_pages = oc_pages+((nn_pages-on_pages);
#else
    ate->l_pages = ol_pages;
    ate->c_pages = oc_pages;
#endif
    ate->base    = naddr+(s_pages*page_size);
    ate->flags   = (uint8_t*)(naddr+((s_pages+nn_pages)*page_size));

#if SBMA_RESIDENT_DEFAULT == 0
  for (i=on_pages; i<nn_pages; ++i)
    ate->flags[i] |= (MMU_CHRGD|MMU_RSDNT);
#endif

    goto DONE;

    CLEANUP1:
    /* insert old ate back into mmu */
    ret = __mmu_insert_ate(&(vmm.mmu), ate);
    ASSERT(-1 != ret);
    CLEANUP:
    for (;;) {
#if SBMA_CHARGE_META == 1
# if SBMA_RESIDENT_DEFAULT == 1
      ret = __ipc_mevict(&(vmm.ipc),\
        VMM_TO_SYS((nn_pages-on_pages)+(nf_pages-of_pages)));
# else
      ret = __ipc_mevict(&(vmm.ipc), VMM_TO_SYS(nf_pages-of_pages));
# endif
#else
# if SBMA_RESIDENT_DEFAULT == 1
      ret = __ipc_mevict(&(vmm.ipc), VMM_TO_SYS((nn_pages-on_pages)));
# else
      ret = 0;
# endif
#endif
      if (-1 == ret)
        return NULL;
      else if (-2 != ret)
        break;
    }

    return NULL;
  }

  DONE:
  //SBMA_STATE_CHECK();
  do {
    int ret = __sbma_check(__func__, __LINE__);
    if (-1 == ret) {
      printf("[%5d] %s:%d %zu,%zu,%zu,%zu,%zu,%zu\n", (int)getpid(),
        __func__, __LINE__, oc_pages, ate->c_pages, chk_c_mem,
        VMM_TO_SYS(chk_c_pages), on_pages, nn_pages);
    }
    ASSERT(-1 != ret);
  } while (0);
  return (void*)ate->base;
}
SBMA_EXPORT(default, void *
__sbma_realloc(void * const __ptr, size_t const __size));


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
#if SBMA_FILE_RESERVE == 1
  ret = truncate(nfname, nn_pages*page_size);
  if (-1 == ret)
    return -1;
#endif

  return 0;
}
SBMA_EXPORT(internal, int
__sbma_remap(void * const __nbase, void * const __obase,
             size_t const __size));
