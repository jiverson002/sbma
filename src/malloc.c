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
#include "config.h"
#include "ipc.h"
#include "lock.h"
#include "mmu.h"
#include "sbma.h"
#include "vmm.h"


/****************************************************************************/
/*! mtouch function prototype. */
/****************************************************************************/
SBMA_EXTERN void    __sbma_check(char const * const __file, int const __line,
                                 int const __flag);
SBMA_EXTERN ssize_t __sbma_mtouch(void * const __addr, size_t const __len);
extern char const * CALL_STR;


/****************************************************************************/
/*! Allocate memory via anonymous mmap. */
/****************************************************************************/
SBMA_EXTERN void *
__sbma_malloc(size_t const __size)
{
  int ret, fd;
  size_t page_size, s_pages, n_pages, f_pages;
  uintptr_t addr;
  struct ate * ate;
  char fname[FILENAME_MAX];

  __sbma_check(__FILE__, __LINE__, 7);
  if (vmm.curpages != vmm.ipc.pmem[vmm.ipc.id]) {
    printf("[%5d,%d] %zu,%zu (%d,%d) <%zu>\n", (int)getpid(), vmm.ipc.id,
      vmm.curpages, vmm.ipc.pmem[vmm.ipc.id], __ipc_is_eligible(&(vmm.ipc)),
      vmm.ipc.flags[vmm.ipc.id], __size);
  }
  ASSERT(0 == __ipc_is_eligible(&(vmm.ipc)));
  ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);
  __sbma_check(__FILE__, __LINE__, 7);

  /* shortcut */
  if (0 == __size)
    return NULL;

  ASSERT(__size > 0);

  /* compute allocation sizes */
  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  n_pages   = 1+((__size-1)/page_size);
  f_pages   = 1+((n_pages*sizeof(uint8_t)-1)/page_size);

  /* check memory file to see if there is enough free memory to complete this
   * allocation. */
  for (;;) {
    ret = __ipc_madmit(&(vmm.ipc), VMM_TO_SYS(s_pages+n_pages+f_pages));
    if (-1 == ret && EAGAIN != errno)
      return NULL;
    else if (-1 != ret)
      break;
  }
  NOSIG_ON;
  ASSERT(IPC_POPULATED == (vmm.ipc.flags[vmm.ipc.id]&IPC_POPULATED));
  NOSIG_ON;

  /* allocate memory */
  addr = (uintptr_t)mmap(NULL, (s_pages+n_pages+f_pages)*page_size,
    PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_LOCKED,\
    -1, 0);
  if ((uintptr_t)MAP_FAILED == addr)
    goto CLEANUP;

  /* read-only protect application pages -- this will avoid the double SIGSEGV
   * for new allocations */
  ret = mprotect((void*)(addr+(s_pages*page_size)), n_pages*page_size,\
    PROT_READ);
  if (-1 == ret)
    goto CLEANUP;

  /* create and truncate the file to size */
  if (0 > snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    addr))
  {
    goto CLEANUP;
  }
  if (-1 == (fd=libc_open(fname, O_WRONLY|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR)))
    goto CLEANUP;
  /*if (-1 == ftruncate(fd, n_pages*page_size))
    return NULL;*/
  if (-1 == close(fd))
    goto CLEANUP;

  /* set pointer for the allocation table entry */
  ate = (struct ate*)addr;

  /* populate ate structure */
  ate->n_pages = n_pages;
  ate->l_pages = n_pages;
  ate->base    = addr+(s_pages*page_size);
  ate->flags   = (uint8_t*)(addr+((s_pages+n_pages)*page_size));

  /* initialize ate lock */
  ret = __lock_init(&(ate->lock));
  if (-1 == ret)
    goto CLEANUP;

  /* insert ate into mmu */
  ret = __mmu_insert_ate(&(vmm.mmu), ate);
  if (-1 == ret)
    goto CLEANUP;

  /* track number of syspages currently loaded, currently allocated, and high
   * water mark number of syspages */
  VMM_TRACK(curpages, VMM_TO_SYS(s_pages+n_pages+f_pages));
  VMM_TRACK(numpages, VMM_TO_SYS(s_pages+n_pages+f_pages));
  VMM_TRACK(maxpages, vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);

  ASSERT(0 == __ipc_is_eligible(&(vmm.ipc)));
  ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);
  __sbma_check(__FILE__, __LINE__, 7);

  NOSIG_OFF;

  return (void*)ate->base;

  CLEANUP:
  for (;;) {
    ret = __ipc_mevict(&(vmm.ipc), VMM_TO_SYS(s_pages+n_pages+f_pages));
    if (-1 == ret && EAGAIN != errno)
      return NULL;
    else if (-1 != ret)
      break;
  }
  ASSERT(0 == __ipc_is_eligible(&(vmm.ipc)));
  ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);
  __sbma_check(__FILE__, __LINE__, 7);
  return NULL;
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
  int ret;
  size_t page_size, s_pages, n_pages, f_pages, l_pages;
  struct ate * ate;
  char fname[FILENAME_MAX];

  ASSERT(0 == __ipc_is_eligible(&(vmm.ipc)));
  ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);
  __sbma_check(__FILE__, __LINE__, 7);

  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  ate       = (struct ate*)((uintptr_t)__ptr-(s_pages*page_size));
  n_pages   = ate->n_pages;
  l_pages   = ate->l_pages;
  f_pages   = 1+((n_pages*sizeof(uint8_t)-1)/page_size);

  /* remove the file */
  if (0 > snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    (uintptr_t)ate))
  {
    return -1;
  }
  ret = unlink(fname);
  if (-1 == ret && ENOENT != errno)
    return -1;

  /* invalidate ate */
  ret = __mmu_invalidate_ate(&(vmm.mmu), ate);
  if (-1 == ret)
    return -1;

  /* destory ate lock */
  ret = __lock_free(&(ate->lock));
  if (-1 == ret)
    return -1;

  /* free resources */
  ret = munmap((void*)ate, (s_pages+n_pages+f_pages)*page_size);
  if (-1 == ret)
    return -1;

  /* track number of syspages currently loaded and allocated */
  VMM_TRACK(curpages, -VMM_TO_SYS(s_pages+l_pages+f_pages));
  VMM_TRACK(numpages, -VMM_TO_SYS(s_pages+n_pages+f_pages));

  /* update memory file */
  for (;;) {
    ret = __ipc_mevict(&(vmm.ipc), VMM_TO_SYS(s_pages+l_pages+f_pages));
    if (-1 == ret && EAGAIN != errno)
      return -1;
    else if (-1 != ret)
      break;
  }

  ASSERT(0 == __ipc_is_eligible(&(vmm.ipc)));
  ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);
  __sbma_check(__FILE__, __LINE__, 7);

  return 0;
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
  size_t i, page_size, s_pages, on_pages, of_pages, ol_pages;
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

  ASSERT(__size > 0);
  ASSERT(0 == __ipc_is_eligible(&(vmm.ipc)));
  ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);
  __sbma_check(__FILE__, __LINE__, 7);

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
    //printf("[%5d] %s:%d\n", (int)getpid(), __func__, __LINE__);
    libc_memmove((void*)(oaddr+((s_pages+nn_pages)*page_size)),\
      (void*)(oaddr+((s_pages+on_pages)*page_size)), nf_pages*page_size);
    //printf("[%5d] %s:%d\n", (int)getpid(), __func__, __LINE__);

    /* unmap unused section of memory */
    ret = munmap((void*)(oaddr+((s_pages+nn_pages+nf_pages)*page_size)),\
      ((on_pages-nn_pages)+(of_pages-nf_pages))*page_size);
    if (-1 == ret)
      return NULL;

    /* track number of syspages currently loaded and allocated */
    VMM_TRACK(curpages,\
      -VMM_TO_SYS((ol_pages-ate->l_pages)+(of_pages-nf_pages)));
    VMM_TRACK(numpages,\
      -VMM_TO_SYS((on_pages-nn_pages)+(of_pages-nf_pages)));

    /* update memory file */
    for (;;) {
      ret = __ipc_mevict(&(vmm.ipc),\
        VMM_TO_SYS((ol_pages-ate->l_pages)+(of_pages-nf_pages)));
      if (-1 == ret && EAGAIN != errno)
        return NULL;
      else if (-1 != ret)
        break;
    }
  }
  else {
    /* check memory file to see if there is enough free memory to complete
     * this allocation. */
    for (;;) {
      ret = __ipc_madmit(&(vmm.ipc),\
        VMM_TO_SYS((nn_pages-on_pages)+(nf_pages-of_pages)));
      if (-1 == ret && EAGAIN != errno)
        return NULL;
      else if (-1 != ret)
        break;
    }
    NOSIG_ON;
    ASSERT(IPC_POPULATED == (vmm.ipc.flags[vmm.ipc.id]&IPC_POPULATED));
    NOSIG_ON;

    /* resize allocation */
#if 1
    naddr = (uintptr_t)mremap((void*)oaddr,\
      (s_pages+on_pages+of_pages)*page_size,\
      (s_pages+nn_pages+nf_pages)*page_size, MREMAP_MAYMOVE);
#else
    naddr = (uintptr_t)mremap((void*)oaddr,\
      (s_pages+on_pages+of_pages)*page_size,\
      (s_pages+nn_pages+nf_pages)*page_size, 0);
#endif
    if ((uintptr_t)MAP_FAILED == naddr)
      goto CLEANUP;

    /* copy page flags to new location */
    //printf("[%5d] %s:%d\n", (int)getpid(), __func__, __LINE__);
    libc_memmove((void*)(naddr+((s_pages+nn_pages)*page_size)),\
      (void*)(naddr+((s_pages+on_pages)*page_size)), of_pages*page_size);
    //printf("[%5d] %s:%d\n", (int)getpid(), __func__, __LINE__);

    /* grant read-only permission to extended area of application memory */
    ret = mprotect((void*)(naddr+((s_pages+on_pages)*page_size)),\
      (nn_pages-on_pages)*page_size, PROT_READ);
    if (-1 == ret)
      goto CLEANUP;

    /* lock new area of allocation into RAM */
    ret = libc_mlock((void*)(naddr+(s_pages+on_pages)*page_size),\
      ((nn_pages-on_pages)+nf_pages)*page_size);
    if (-1 == ret)
      goto CLEANUP;

    if (0 > snprintf(nfname, FILENAME_MAX, "%s%d-%zx", vmm.fstem,\
      (int)getpid(), naddr))
    {
      return NULL;
    }
    /* if the allocation has moved */
    if (oaddr != naddr) {
      /* move old file to new file and trucate to size */
      if (0 > snprintf(ofname, FILENAME_MAX, "%s%d-%zx", vmm.fstem,\
        (int)getpid(), oaddr))
      {
        goto CLEANUP;
      }
      if (-1 == rename(ofname, nfname))
        goto CLEANUP;

      /* set pointer for the allocation table entry */
      ate = (struct ate*)naddr;

      /* remove old ate from mmu */
      ret = __mmu_invalidate_ate(&(vmm.mmu), ate);
      if (-1 == ret)
        goto CLEANUP;

      /* insert new ate into mmu */
      ret = __mmu_insert_ate(&(vmm.mmu), ate);
      if (-1 == ret)
        goto CLEANUP;
    }
    /*if (-1 == truncate(nfname, nn_pages*page_size))
      return NULL;*/

    /* populate ate structure */
    ate->n_pages = nn_pages;
    ate->l_pages = ol_pages+((nn_pages-on_pages)+(nf_pages-of_pages));
    ate->base    = naddr+(s_pages*page_size);
    ate->flags   = (uint8_t*)(naddr+((s_pages+nn_pages)*page_size));

    /* track number of syspages currently loaded, currently allocated, and
     * high water mark number of syspages */
    VMM_TRACK(curpages, VMM_TO_SYS((nn_pages-on_pages)+(nf_pages-of_pages)));
    VMM_TRACK(numpages, VMM_TO_SYS((nn_pages-on_pages)+(nf_pages-of_pages)));
    VMM_TRACK(maxpages,\
      vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);

    NOSIG_OFF;

    CLEANUP:
    for (;;) {
      ret = __ipc_mevict(&(vmm.ipc),\
        VMM_TO_SYS((nn_pages-on_pages)+(nf_pages-of_pages)));
      if (-1 == ret && EAGAIN != errno)
        return NULL;
      else if (-1 != ret)
        break;
    }
    ASSERT(0 == __ipc_is_eligible(&(vmm.ipc)));
    ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);
    __sbma_check(__FILE__, __LINE__, 7);
    return NULL;
  }

  ASSERT(0 == __ipc_is_eligible(&(vmm.ipc)));
  ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);
  __sbma_check(__FILE__, __LINE__, 7);

  return (void*)ate->base;
}
SBMA_EXPORT(default, void *
__sbma_realloc(void * const __ptr, size_t const __size));


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

  ASSERT(0 == __ipc_is_eligible(&(vmm.ipc)));
  ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);
  __sbma_check(__FILE__, __LINE__, 7);

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
  ASSERT(nate->l_pages == nate->n_pages);

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)nptr-nate->base)/page_size;
  end = 1+(((uintptr_t)nptr+__size-nate->base-1)/page_size);

  /* load new and old memory */
  CALL_STR = __func__;
  //printf("[%5d] %s:%d\n", (int)getpid(), __func__, __LINE__);
  ret = __sbma_mtouch_atomic(nptr, __size, optr, __size, SBMA_ATOMIC_END);
  if (-1 == ret)
    return -1;

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

  /* move old file to new file and truncate to size */
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

  ASSERT(0 == __ipc_is_eligible(&(vmm.ipc)));
  ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);
  __sbma_check(__FILE__, __LINE__, 7);

  return 0;
}
SBMA_EXPORT(internal, int
__sbma_remap(void * const __nbase, void * const __obase, size_t const __size,
             size_t const __off));
