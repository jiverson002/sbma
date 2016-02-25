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


#include <stdint.h>    /* uint8_t */
#include <stddef.h>    /* NULL, size_t */
#include "common.h"
#include "lock.h"
#include "sbma.h"
#include "vmm.h"


/****************************************************************************/
/*! Check to make sure that the state of the vmm is consistent. */
/****************************************************************************/
SBMA_EXTERN int
sbma_mcheck(char const * const __func, int const __line)
{
  int ret, retval=0;
  size_t i, c, l, d;
  size_t c_pages=0, d_pages=0, s_pages, f_pages;
  struct ate * ate;

  if (VMM_CHECK == (_vmm_.opts&VMM_CHECK)) {
    ret = lock_get(&(_vmm_.lock));
    if (-1 == ret)
      goto CLEANUP1;

    for (ate=_vmm_.mmu.a_tbl; NULL!=ate; ate=ate->next) {
      ret = lock_get(&(ate->lock));
      if (-1 == ret)
        goto CLEANUP2;

      if (VMM_METACH == (_vmm_.opts&VMM_METACH)) {
        s_pages  = 1+((sizeof(struct ate)-1)/_vmm_.page_size);
        f_pages  = 1+((ate->n_pages*sizeof(uint8_t)-1)/_vmm_.page_size);
      }
      else {
        s_pages  = 0;
        f_pages  = 0;
      }
      c_pages += s_pages+ate->c_pages+f_pages;
      d_pages += ate->d_pages;

      if (VMM_EXTRA == (_vmm_.opts&VMM_EXTRA)) {
        for (l=0,c=0,d=0,i=0; i<ate->n_pages; ++i) {
          if (MMU_RSDNT != (ate->flags[i]&MMU_RSDNT))
            l++;
          if (MMU_CHRGD != (ate->flags[i]&MMU_CHRGD))
            c++;
          if (MMU_DIRTY == (ate->flags[i]&MMU_DIRTY))
            d++;
        }
        if (l != ate->l_pages) {
          printf("[%5d] %s:%d l (%zu) != l_pages (%zu)\n", (int)getpid(),
            __func, __line, l, ate->l_pages);
          goto CLEANUP2;
        }
        if (c != ate->c_pages) {
          printf("[%5d] %s:%d c (%zu) != c_pages (%zu)\n", (int)getpid(),
            __func, __line, c, ate->c_pages);
          goto CLEANUP2;
        }
        if (d != ate->d_pages) {
          printf("[%5d] %s:%d d (%zu) != d_pages (%zu)\n", (int)getpid(),
            __func, __line, d, ate->d_pages);
          goto CLEANUP2;
        }
      }

      ret = lock_let(&(ate->lock));
      if (-1 == ret)
        goto CLEANUP2;
    }

    if (VMM_TO_SYS(c_pages) != _vmm_.ipc.c_mem[_vmm_.ipc.id]) {
      printf("[%5d] %s:%d c_pages (%zu) != c_mem[id] (%zu)\n", (int)getpid(),
        __func, __line, VMM_TO_SYS(c_pages), _vmm_.ipc.c_mem[_vmm_.ipc.id]);
      retval = -1;
    }
    if (VMM_TO_SYS(d_pages) != _vmm_.ipc.d_mem[_vmm_.ipc.id]) {
      printf("[%5d] %s:%d d_pages (%zu) != d_mem[id] (%zu)\n", (int)getpid(),
        __func, __line, VMM_TO_SYS(d_pages), _vmm_.ipc.d_mem[_vmm_.ipc.id]);
      retval = -1;
    }

    ret = lock_let(&(_vmm_.lock));
    if (-1 == ret)
      goto CLEANUP1;
  }
  else {
    retval = 0;
  }

  return retval;

  CLEANUP2:
  ret = lock_let(&(ate->lock));
  ASSERT(-1 != ret);
  CLEANUP1:
  ret = lock_let(&(_vmm_.lock));
  ASSERT(-1 != ret);
  return -1;
}


#ifdef TEST
int
main(int argc, char * argv[])
{
  if (0 == argc || NULL == argv) {}

  return 0;
}
#endif
