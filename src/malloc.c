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
# define _GNU_SOURCE 1
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
    if (VMM_METACH == (vmm.opts&VMM_METACH)) {
      if (VMM_RSDNT == (vmm.opts&VMM_RSDNT)) {
        ret = ipc_madmit(&(vmm.ipc), VMM_TO_SYS(s_pages+n_pages+f_pages),\
          vmm.opts&VMM_ADMITD);
      }
      else {
        ret = ipc_madmit(&(vmm.ipc), VMM_TO_SYS(s_pages+f_pages),\
          vmm.opts&VMM_ADMITD);
      }
    }
    else {
      if (VMM_RSDNT == (vmm.opts&VMM_RSDNT))
        ret = ipc_madmit(&(vmm.ipc), VMM_TO_SYS(n_pages),\
          vmm.opts&VMM_ADMITD);
      else
        ret = 0;
    }
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

  if (VMM_RSDNT == (vmm.opts&VMM_RSDNT)) {
    /* Read-only protect application pages -- this will avoid the double
     * SIGSEGV for new allocations. */
    ret = mprotect((void*)(addr+(s_pages*page_size)), n_pages*page_size,\
      PROT_READ);
  }
  else {
    /* Remove all protectection from application pages -- this reduces the
     * amount of memory which is admitted by default. */
    ret = mprotect((void*)(addr+(s_pages*page_size)), n_pages*page_size,\
      PROT_NONE);
  }
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
    goto CLEANUP3;
#endif

  /* Set and populate ate structure. */
  ate          = (struct ate*)addr;
  ate->n_pages = n_pages;
  if (VMM_RSDNT == (vmm.opts&VMM_RSDNT)) {
    ate->l_pages = n_pages;
    ate->c_pages = n_pages;
  }
  else {
    ate->l_pages = 0;
    ate->c_pages = 0;
  }
  ate->d_pages = 0;
  ate->base    = addr+(s_pages*page_size);
  ate->flags   = (uint8_t*)(addr+((s_pages+n_pages)*page_size));

  if (VMM_RSDNT != (vmm.opts&VMM_RSDNT)) {
    for (i=0; i<n_pages; ++i)
      ate->flags[i] |= (MMU_CHRGD|MMU_RSDNT);
  }

  /* Initialize ate lock. */
  ret = lock_init(&(ate->lock));
  if (-1 == ret)
    goto CLEANUP3;

  /* Insert ate into mmu. */
  ret = mmu_insert_ate(&(vmm.mmu), ate);
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
  ret = mmu_invalidate_ate(&(vmm.mmu), ate);
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
    if (VMM_METACH == (vmm.opts&VMM_METACH)) {
      if (VMM_RSDNT == (vmm.opts&VMM_RSDNT))
        ret = ipc_mevict(&(vmm.ipc), VMM_TO_SYS(s_pages+n_pages+f_pages), 0);
      else
        ret = ipc_mevict(&(vmm.ipc), VMM_TO_SYS(s_pages+f_pages), 0);
    }
    else {
      if (VMM_RSDNT == (vmm.opts&VMM_RSDNT))
        ret = ipc_mevict(&(vmm.ipc), VMM_TO_SYS(n_pages), 0);
      else
        ret = 0;
    }
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
  size_t page_size, s_pages, n_pages, f_pages, c_pages, d_pages;
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
  d_pages   = ate->d_pages;
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
  ret = mmu_invalidate_ate(&(vmm.mmu), ate);
  if (-1 == ret)
    retval = -1;

  /* Destory ate lock. */
  ret = lock_free(&(ate->lock));
  if (-1 == ret)
    retval = -1;

  /* Free resources. */
  ret = munmap((void*)ate, (s_pages+n_pages+f_pages)*page_size);
  if (-1 == ret)
    retval = -1;

  /* Update memory file. */
  for (;;) {
    if (VMM_METACH == (vmm.opts&VMM_METACH))
      retval = ipc_mevict(&(vmm.ipc), VMM_TO_SYS(s_pages+c_pages+f_pages),\
        VMM_TO_SYS(d_pages));
    else
      retval = ipc_mevict(&(vmm.ipc), VMM_TO_SYS(c_pages),\
        VMM_TO_SYS(d_pages));
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
  size_t od_pages, nn_pages, nf_pages;
  uint8_t oflag;
  uintptr_t oaddr, naddr;
  void * retval;
  volatile uint8_t * oflags, * nflags;
  struct ate * ate;
  char ofname[FILENAME_MAX], nfname[FILENAME_MAX];

  /* TODO: Need to make sure that in case of an error, the state of vmm.ipc is
   * correct. For instance, in the 'else' case, the first step taken is to
   * call madmit, then if the calls fails, the vmm.ipc state will be
   * incorrect. So, if an error occurs, then mevict must be called to offset
   * and correct the state of vmm.ipc. */

  SBMA_STATE_CHECK();

  if (0 == __size)
    return NULL;

  /* Default return value. */
  retval = NULL;

  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  ate       = (struct ate*)((uintptr_t)__ptr-(s_pages*page_size));
  oaddr     = (uintptr_t)ate;
  oflags    = ate->flags;
  on_pages  = ate->n_pages;
  of_pages  = 1+((on_pages*sizeof(uint8_t)-1)/page_size);
  nn_pages  = 1+((__size-1)/page_size);
  nf_pages  = 1+((nn_pages*sizeof(uint8_t)-1)/page_size);

  if (nn_pages == on_pages) {
    /* do nothing */
    retval = (void*)ate->base;
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
      if (MMU_DIRTY == (oflags[i]&MMU_DIRTY)) {
        ASSERT(ate->d_pages > 0);
        ate->d_pages--;
      }
    }

    /* update protection for new page flags area of allocation */
    ret = mprotect((void*)(oaddr+((s_pages+nn_pages)*page_size)),\
      nf_pages*page_size, PROT_READ|PROT_WRITE);
    if (-1 == ret)
      return NULL;

    if (VMM_MLOCK == (vmm.opts&VMM_MLOCK)) {
      /* lock new page flags area of allocation into RAM */
      ret = libc_mlock((void*)(oaddr+((s_pages+nn_pages)*page_size)),\
        nf_pages*page_size);
      if (-1 == ret)
        return NULL;
    }

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
      ol_pages = ate->l_pages;
      oc_pages = ate->c_pages;
      od_pages = ate->d_pages;

      ret = ipc_mevict(&(vmm.ipc),\
        VMM_TO_SYS((oc_pages-ate->c_pages)+(of_pages-nf_pages)),\
        VMM_TO_SYS(od_pages));
      if (-1 == ret)
        return NULL;
      else if (-2 != ret)
        break;
    }

    retval = (void*)ate->base;
  }
  else {
    /* check memory file to see if there is enough free memory to complete
     * this allocation. */
    for (;;) {
      ol_pages = ate->l_pages;
      oc_pages = ate->c_pages;

      if (VMM_METACH == (vmm.opts&VMM_METACH)) {
        if (VMM_RSDNT == (vmm.opts&VMM_RSDNT)) {
          ret = ipc_madmit(&(vmm.ipc),\
            VMM_TO_SYS((nn_pages-on_pages)+(nf_pages-of_pages)),\
            vmm.opts&VMM_ADMITD);
        }
        else {
          ret = ipc_madmit(&(vmm.ipc), VMM_TO_SYS(nf_pages-of_pages),\
            vmm.opts&VMM_ADMITD);
        }
      }
      else {
        if (VMM_RSDNT == (vmm.opts&VMM_RSDNT)) {
          ret = ipc_madmit(&(vmm.ipc), VMM_TO_SYS(nn_pages-on_pages),\
            vmm.opts&VMM_ADMITD);
        }
        else
          ret = 0;
      }
      if (-1 == ret)
        goto RETURN;
      else if (-2 != ret)
        break;
    }

    /* remove old ate from mmu */
    ret = mmu_invalidate_ate(&(vmm.mmu), ate);
    if (-1 == ret)
      goto CLEANUP;

    if (VMM_MERGE == (vmm.opts&VMM_MERGE)) {
      /* TODO: I think the reason that mremap fails so frequently is due to the
       * fact that oaddr is not seen as a single vma in the kernel, but rather
       * as several vmas, due to the use of mprotect to manage access to the
       * memory region. */
      /* Make sure the kernel sees the entire range as a single vma. */
      ret = mprotect((void*)oaddr, (s_pages+on_pages+of_pages)*page_size,\
        PROT_READ|PROT_WRITE);
      if (-1 == ret)
        goto CLEANUP1;
    }

    /* resize allocation */
    naddr = (uintptr_t)mremap((void*)oaddr,\
      (s_pages+on_pages+of_pages)*page_size,\
      (s_pages+nn_pages+nf_pages)*page_size, MREMAP_MAYMOVE);
    if ((uintptr_t)MAP_FAILED == naddr)
      goto CLEANUP2;

    /* copy page flags to new location */
    libc_memmove((void*)(naddr+((s_pages+nn_pages)*page_size)),\
      (void*)(naddr+((s_pages+on_pages)*page_size)), of_pages*page_size);

    if (VMM_MERGE == (vmm.opts&VMM_MERGE)) {
      if (VMM_RSDNT == (vmm.opts&VMM_RSDNT)) {
        /* grant read-only permission application memory */
        ret = mprotect((void*)(naddr+(s_pages*page_size)),\
          nn_pages*page_size, PROT_READ);
      }
      else {
        /* grant no permission to application memory */
        ret = mprotect((void*)(naddr+(s_pages*page_size)),\
          nn_pages*page_size, PROT_NONE);
      }
    }
    else {
      if (VMM_RSDNT == (vmm.opts&VMM_RSDNT)) {
        /* grant read-only permission to extended area of application memory
         * */
        ret = mprotect((void*)(naddr+((s_pages+on_pages)*page_size)),\
          (nn_pages-on_pages)*page_size, PROT_READ);
      }
      else {
        /* grant no permission to extended area of application memory */
        ret = mprotect((void*)(naddr+((s_pages+on_pages)*page_size)),\
          (nn_pages-on_pages)*page_size, PROT_NONE);
      }
    }
    ERRCHK(FATAL, -1 == ret);

    if (VMM_MERGE == (vmm.opts&VMM_MERGE)) {
#if 1
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
          ERRCHK(FATAL, -1 == ret);

          if (i != on_pages) {
            ifirst = i;
            oflag  = nflags[i];
          }
        }
      }
#else
      nflags = (uint8_t*)(naddr+((s_pages+nn_pages)*page_size));
      for (i=0; i<on_pages; ++i) {
        if (MMU_DIRTY == (nflags[i]&MMU_DIRTY)) {
          ret = mprotect((void*)(naddr+(s_pages+i)*page_size), page_size,\
            PROT_READ|PROT_WRITE);
        }
        else if (MMU_RSDNT != (nflags[i]&MMU_RSDNT)) {
          ret = mprotect((void*)(naddr+(s_pages+i)*page_size), page_size,\
            PROT_READ);
        }
        ERRCHK(FATAL, -1 == ret);
      }
#endif
    }

    if (VMM_MLOCK == (vmm.opts&VMM_MLOCK)) {
      if (VMM_RSDNT == (vmm.opts&VMM_RSDNT)) {
        if (VMM_MERGE == (vmm.opts&VMM_MERGE)) {
          /* lock application memory into RAM */
          ret = libc_mlock((void*)(naddr+s_pages*page_size),\
            nn_pages*page_size);
        }
        else {
          /* lock application memory into RAM */
          ret = libc_mlock((void*)(naddr+(s_pages+on_pages)*page_size),\
            (nn_pages-on_pages)*page_size);
        }
        ERRCHK(FATAL, -1 == ret);
      }
      /* lock book-keeping memory into RAM */
      ret = libc_mlock((void*)(naddr+(s_pages+nn_pages)*page_size),\
        nf_pages*page_size);
      ERRCHK(FATAL, -1 == ret);
    }

    ret = snprintf(nfname, FILENAME_MAX, "%s%d-%zx", vmm.fstem,\
      (int)getpid(), naddr);
    ERRCHK(FATAL, 0 > ret);
    /* if the allocation has moved */
    if (oaddr != naddr) {
      /* move old file to new file and trucate to size */
      ret = snprintf(ofname, FILENAME_MAX, "%s%d-%zx", vmm.fstem,\
        (int)getpid(), oaddr);
      ERRCHK(FATAL, 0 > ret);
      ret = rename(ofname, nfname);
      ERRCHK(FATAL, -1 == ret);

      /* set pointer for the allocation table entry */
      ate = (struct ate*)naddr;
    }
#if SBMA_FILE_RESERVE == 1
    ret = truncate(nfname, nn_pages*page_size);
    ERRCHK(FATAL, -1 == ret);
#endif

    /* insert new ate into mmu */
    ret = mmu_insert_ate(&(vmm.mmu), ate);
    ERRCHK(FATAL, -1 == ret);

    /* populate ate structure */
    ate->n_pages = nn_pages;
    if (VMM_RSDNT == (vmm.opts&VMM_RSDNT)) {
      ate->l_pages = ol_pages+(nn_pages-on_pages);
      ate->c_pages = oc_pages+(nn_pages-on_pages);
    }
    else {
      ate->l_pages = ol_pages;
      ate->c_pages = oc_pages;
    }
    ate->base  = naddr+(s_pages*page_size);
    ate->flags = (uint8_t*)(naddr+((s_pages+nn_pages)*page_size));

    if (VMM_RSDNT != (vmm.opts&VMM_RSDNT)) {
      for (i=on_pages; i<nn_pages; ++i)
        ate->flags[i] |= (MMU_CHRGD|MMU_RSDNT);
    }

    /************************************************************************/
    /* Successful exit -- return pointer to appliction memory. */
    /************************************************************************/
    retval = (void*)ate->base;
    goto RETURN;

    /************************************************************************/
    /* Error exit -- revert changes to vmm state, release any memory, and
     * remove any files created, then return NULL. */
    /************************************************************************/
    CLEANUP2:
    if (VMM_MERGE == (vmm.opts&VMM_MERGE)) {
      /* grant no permission to application memory */
      ret = mprotect((void*)(oaddr+(s_pages*page_size)), on_pages*page_size,\
        PROT_NONE);
      ASSERT(-1 != ret);

      /* revert memory protection according to existing flags */
      oflags = (uint8_t*)(oaddr+((s_pages+on_pages)*page_size));
      for (i=0; i<on_pages; ++i) {
        if (MMU_DIRTY == (oflags[i]&MMU_DIRTY)) {
          ret = mprotect((void*)(oaddr+(s_pages+i)*page_size), page_size,\
            PROT_READ|PROT_WRITE);
        }
        else if (MMU_RSDNT != (oflags[i]&MMU_RSDNT)) {
          ret = mprotect((void*)(oaddr+(s_pages+i)*page_size), page_size,\
            PROT_READ);
        }
        ASSERT(-1 != ret);
      }
    }
    CLEANUP1:
    /* insert old ate back into mmu */
    ret = mmu_insert_ate(&(vmm.mmu), ate);
    ASSERT(-1 != ret);
    CLEANUP:
    for (;;) {
      if (VMM_METACH == (vmm.opts&VMM_METACH)) {
        if (VMM_RSDNT == (vmm.opts&VMM_RSDNT)) {
          ret = ipc_mevict(&(vmm.ipc),\
            VMM_TO_SYS((nn_pages-on_pages)+(nf_pages-of_pages)), 0);
        }
        else {
          ret = ipc_mevict(&(vmm.ipc), VMM_TO_SYS(nf_pages-of_pages), 0);
        }
      }
      else {
        if (VMM_RSDNT == (vmm.opts&VMM_RSDNT))
          ret = ipc_mevict(&(vmm.ipc), VMM_TO_SYS((nn_pages-on_pages)), 0);
        else
          ret = 0;
      }
      if (-1 == ret)
        goto RETURN;
      else if (-2 != ret)
        break;
    }

    goto RETURN;
  }

  /**************************************************************************/
  /* Return point -- make sure vmm is in valid state and return. */
  /**************************************************************************/
  RETURN:
  SBMA_STATE_CHECK();
  return retval;

  /**************************************************************************/
  /* Fatal error -- an unrecoverable error has occured, the previously
   * allocated memory has been lost and no new memory could be successfully
   * allocated. */
  /**************************************************************************/
  FATAL:
  FATAL_ABORT(errno);
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
    ASSERT(MMU_DIRTY != (oflags[i]&MMU_DIRTY)); /* not dirty */
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
