/*
Copyright (c) 2015,2016 Jeremy Iverson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/


#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif


#include <errno.h>     /* errno library */
#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t */
#include <stdio.h>     /* FILENAME_MAX */
#include <sys/mman.h>  /* mremap, munmap, mprotect */
#include <sys/types.h> /* truncate */
#include <unistd.h>    /* truncate */
#include "common.h"
#include "ipc.h"
#include "mmu.h"
#include "sbma.h"
#include "vmm.h"


/****************************************************************************/
/*! Re-allocate memory via anonymous mmap. */
/****************************************************************************/
SBMA_EXTERN void *
sbma_realloc(void * const __ptr, size_t const __size)
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

  /* TODO: Need to make sure that in case of an error, the state of _vmm_.ipc is
   * correct. For instance, in the 'else' case, the first step taken is to
   * call madmit, then if the calls fails, the _vmm_.ipc state will be
   * incorrect. So, if an error occurs, then mevict must be called to offset
   * and correct the state of _vmm_.ipc. */

  SBMA_STATE_CHECK();

  if (0 == __size)
    return NULL;

  /* Default return value. */
  retval = NULL;

  page_size = _vmm_.page_size;
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

    if (VMM_MLOCK == (_vmm_.opts&VMM_MLOCK)) {
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

      ret = ipc_mevict(&(_vmm_.ipc),\
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

      if (VMM_METACH == (_vmm_.opts&VMM_METACH)) {
        if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT)) {
          ret = ipc_madmit(&(_vmm_.ipc),\
            VMM_TO_SYS((nn_pages-on_pages)+(nf_pages-of_pages)),\
            _vmm_.opts&VMM_ADMITD);
        }
        else {
          ret = ipc_madmit(&(_vmm_.ipc), VMM_TO_SYS(nf_pages-of_pages),\
            _vmm_.opts&VMM_ADMITD);
        }
      }
      else {
        if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT)) {
          ret = ipc_madmit(&(_vmm_.ipc), VMM_TO_SYS(nn_pages-on_pages),\
            _vmm_.opts&VMM_ADMITD);
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
    ret = mmu_invalidate_ate(&(_vmm_.mmu), ate);
    if (-1 == ret)
      goto CLEANUP;

    if (VMM_MERGE == (_vmm_.opts&VMM_MERGE)) {
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

    if (VMM_MERGE == (_vmm_.opts&VMM_MERGE)) {
      if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT)) {
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
      if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT)) {
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

    if (VMM_MERGE == (_vmm_.opts&VMM_MERGE)) {
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

    if (VMM_MLOCK == (_vmm_.opts&VMM_MLOCK)) {
      if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT)) {
        if (VMM_MERGE == (_vmm_.opts&VMM_MERGE)) {
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

    ret = snprintf(nfname, FILENAME_MAX, "%s%d-%zx", _vmm_.fstem,\
      (int)getpid(), naddr);
    ERRCHK(FATAL, 0 > ret);
    /* if the allocation has moved */
    if (oaddr != naddr) {
      /* move old file to new file and trucate to size */
      ret = snprintf(ofname, FILENAME_MAX, "%s%d-%zx", _vmm_.fstem,\
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
    ret = mmu_insert_ate(&(_vmm_.mmu), ate);
    ERRCHK(FATAL, -1 == ret);

    /* populate ate structure */
    ate->n_pages = nn_pages;
    if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT)) {
      ate->l_pages = ol_pages+(nn_pages-on_pages);
      ate->c_pages = oc_pages+(nn_pages-on_pages);
    }
    else {
      ate->l_pages = ol_pages;
      ate->c_pages = oc_pages;
    }
    ate->base  = naddr+(s_pages*page_size);
    ate->flags = (uint8_t*)(naddr+((s_pages+nn_pages)*page_size));

    if (VMM_RSDNT != (_vmm_.opts&VMM_RSDNT)) {
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
    if (VMM_MERGE == (_vmm_.opts&VMM_MERGE)) {
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
    ret = mmu_insert_ate(&(_vmm_.mmu), ate);
    ASSERT(-1 != ret);
    CLEANUP:
    for (;;) {
      if (VMM_METACH == (_vmm_.opts&VMM_METACH)) {
        if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT)) {
          ret = ipc_mevict(&(_vmm_.ipc),\
            VMM_TO_SYS((nn_pages-on_pages)+(nf_pages-of_pages)), 0);
        }
        else {
          ret = ipc_mevict(&(_vmm_.ipc), VMM_TO_SYS(nf_pages-of_pages), 0);
        }
      }
      else {
        if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT))
          ret = ipc_mevict(&(_vmm_.ipc), VMM_TO_SYS((nn_pages-on_pages)), 0);
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


#ifdef TEST
int
main(int argc, char * argv[])
{
  if (0 == argc || NULL == argv) {}

  return 0;
}
#endif
