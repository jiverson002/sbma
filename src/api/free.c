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
#include <stddef.h>    /* size_t */
#include <stdio.h>     /* FILENAME_MAX */
#include <sys/mman.h>  /* munmap */
#include "common.h"
#include "ipc.h"
#include "lock.h"
#include "mmu.h"
#include "sbma.h"
#include "vmm.h"


/****************************************************************************/
/*! Frees the memory associated with an anonymous mmap. */
/****************************************************************************/
SBMA_EXTERN int
sbma_free(void * const __ptr)
{
  int ret, retval;
  size_t page_size, s_pages, n_pages, f_pages, c_pages, d_pages;
  struct ate * ate;
  char fname[FILENAME_MAX];

  SBMA_STATE_CHECK();

  /* Default return value. */
  retval = 0;

  page_size = _vmm_.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  ate       = (struct ate*)((uintptr_t)__ptr-(s_pages*page_size));
  n_pages   = ate->n_pages;
  c_pages   = ate->c_pages;
  d_pages   = ate->d_pages;
  f_pages   = 1+((n_pages*sizeof(uint8_t)-1)/page_size);

  /* Remove the file. */
  ret = snprintf(fname, FILENAME_MAX, "%s%d-%zx", _vmm_.fstem, (int)getpid(),\
    (uintptr_t)ate);
  if (0 > ret)
    retval = -1;
  ret = unlink(fname);
  if (-1 == ret && ENOENT != errno)
    retval = -1;

  /* Invalidate ate. */
  ret = mmu_invalidate_ate(&(_vmm_.mmu), ate);
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
    if (VMM_METACH == (_vmm_.opts&VMM_METACH))
      retval = ipc_mevict(&(_vmm_.ipc), VMM_TO_SYS(s_pages+c_pages+f_pages),\
        VMM_TO_SYS(d_pages));
    else
      retval = ipc_mevict(&(_vmm_.ipc), VMM_TO_SYS(c_pages),\
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


#ifdef TEST
int
main(int argc, char * argv[])
{
  if (0 == argc || NULL == argv) {}

  return 0;
}
#endif
