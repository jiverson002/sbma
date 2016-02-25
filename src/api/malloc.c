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


#include <fcntl.h>     /* O_WRONLY, O_CREAT, O_EXCL */
#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t */
#include <stdio.h>     /* FILENAME_MAX */
#include <sys/mman.h>  /* mmap, munmap, mprotect */
#include <sys/stat.h>  /* S_IRUSR, S_IWUSR */
#include <sys/types.h> /* truncate */
#include <unistd.h>    /* truncate */
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
sbma_malloc(size_t const __size)
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
  page_size = _vmm_.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);      /* struct pages */
  n_pages   = 1+((__size-1)/page_size);                  /* app pages */
  f_pages   = 1+((n_pages*sizeof(uint8_t)-1)/page_size); /* flag pages */

  /* Check memory file to see if there is enough free memory to complete this
   * allocation. */
  for (;;) {
    if (VMM_METACH == (_vmm_.opts&VMM_METACH)) {
      if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT)) {
        ret = ipc_madmit(&(_vmm_.ipc), VMM_TO_SYS(s_pages+n_pages+f_pages),\
          _vmm_.opts&VMM_ADMITD);
      }
      else {
        ret = ipc_madmit(&(_vmm_.ipc), VMM_TO_SYS(s_pages+f_pages),\
          _vmm_.opts&VMM_ADMITD);
      }
    }
    else {
      if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT))
        ret = ipc_madmit(&(_vmm_.ipc), VMM_TO_SYS(n_pages),\
          _vmm_.opts&VMM_ADMITD);
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

  if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT)) {
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
  ret = snprintf(fname, FILENAME_MAX, "%s%d-%zx", _vmm_.fstem, (int)getpid(),\
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
  if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT)) {
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

  if (VMM_RSDNT != (_vmm_.opts&VMM_RSDNT)) {
    for (i=0; i<n_pages; ++i)
      ate->flags[i] |= (MMU_CHRGD|MMU_RSDNT);
  }

  /* Initialize ate lock. */
  ret = lock_init(&(ate->lock));
  if (-1 == ret)
    goto CLEANUP3;

  /* Insert ate into mmu. */
  ret = mmu_insert_ate(&(_vmm_.mmu), ate);
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
  ret = mmu_invalidate_ate(&(_vmm_.mmu), ate);
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
    if (VMM_METACH == (_vmm_.opts&VMM_METACH)) {
      if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT))
        ret = ipc_mevict(&(_vmm_.ipc), VMM_TO_SYS(s_pages+n_pages+f_pages), 0);
      else
        ret = ipc_mevict(&(_vmm_.ipc), VMM_TO_SYS(s_pages+f_pages), 0);
    }
    else {
      if (VMM_RSDNT == (_vmm_.opts&VMM_RSDNT))
        ret = ipc_mevict(&(_vmm_.ipc), VMM_TO_SYS(n_pages), 0);
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


#ifdef TEST
int
main(int argc, char * argv[])
{
  if (0 == argc || NULL == argv) {}

  return 0;
}
#endif
