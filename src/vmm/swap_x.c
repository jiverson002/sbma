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


#include <stddef.h>   /* NULL, size_t */
#include <stdint.h>   /* uint8_t */
#include <sys/mman.h> /* mprotect */
#include "common.h"
#include "sbma.h"
#include "vmm.h"


/*****************************************************************************/
/*  Clear dirty and zfill flags from a range of pages, changing memory       */
/*  protections when necessary.                                              */
/*                                                                           */
/*  MT-Unsafe race:ate->*                                                    */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  Call only when thread is in possession of ate->lock.               */
/*****************************************************************************/
SBMA_EXTERN ssize_t
vmm_swap_x(struct ate * const ate, size_t const beg, size_t const num)
{
  int retval, ret;
  size_t ip, end, page_size;
  volatile uint8_t * flags;

  /* Sanity check input values. */
  ASSERT(NULL != ate);
  ASSERT(num <= ate->n_pages);
  ASSERT(beg <= ate->n_pages-num);

  /* Default return value. */
  retval = 0;

  /* Shortcut if no pages in range. */
  if (0 == num)
    goto RETURN;
  /* TODO Shortcut if there are no dirty pages AND no pages stored on disk. */

  /* Setup local variables. */
  page_size = _vmm_.page_size;
  flags     = ate->flags;
  end       = beg+num;

  /* Loop over pages, updating protection for dirty pages and removing
   * dirty/zfill flags from all pages. */
  for (ip=beg; ip<end; ++ip) {
    if (MMU_DIRTY == (flags[ip]&MMU_DIRTY)) {
      ret = mprotect((void*)(ate->base+(ip*page_size)), page_size,\
        PROT_READ);
      ERRCHK(ERREXIT, -1 == ret);

      ASSERT(ate->d_pages > 0);
      ate->d_pages--;
    }

    /* flag: *0*0 */
    flags[ip] &= ~(MMU_DIRTY|MMU_ZFILL);
  }

  /***************************************************************************/
  /* Successful exit -- return 0. */
  /***************************************************************************/
  goto RETURN;

  /***************************************************************************/
  /* Error exit -- return -1. */
  /***************************************************************************/
  ERREXIT:
  retval = -1;

  /***************************************************************************/
  /* Return point -- return. */
  /***************************************************************************/
  RETURN:
  return retval;
}
