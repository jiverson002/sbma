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


#include <fcntl.h>    /* O_WRONLY */
#include <stddef.h>   /* NULL, size_t */
#include <stdint.h>   /* uint8_t, uintptr_t */
#include <stdio.h>    /* FILENAME_MAX */
#include <string.h>   /* snprintf */
#include <sys/mman.h> /* madvise, mprotect */
#include "common.h"
#include "ipc.h"
#include "lock.h"
#include "mmu.h"
#include "sbma.h"
#include "vmm.h"


/*****************************************************************************/
/*  Write data to file.                                                      */
/*                                                                           */
/*  MT-Unsafe race:buf                                                       */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  Call only when thread is in possession of ate->lock which          */
/*        corresponds to the buffer in question.                             */
/*****************************************************************************/
SBMA_STATIC int
vmm_write(int const fd, void const * const buf, size_t len, size_t off)
{
  ssize_t len_;
  char * buf_ = (char*)buf;

#ifndef HAVE_PWRITE
  if (-1 == lseek(fd, off, SEEK_SET))
    return -1;
#endif

  do {
#ifdef HAVE_PWRITE
    if (-1 == (len_=libc_pwrite(fd, buf_, len, off)))
      return -1;
    off += len_;
#else
    if (-1 == (len_=libc_write(fd, buf_, len)))
      return -1;
#endif

    ASSERT(0 != len_);

    buf_ += len_;
    len -= len;
  } while (len > 0);

  return 0;
}


/*****************************************************************************/
/*  Write dirty pages to file, remove zfill flag from those pages, and       */
/*  update their memory protections.                                         */
/*                                                                           */
/*  MT-Unsafe race:ate->*                                                    */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  Call only when thread is in possession of ate->lock.               */
/*****************************************************************************/
SBMA_EXTERN ssize_t
vmm_swap_o(struct ate * const ate, size_t const beg, size_t const num)
{
  int retval, ret, fd;
  size_t ip, page_size, end, numwr=0;
  ssize_t ipfirst;
  uintptr_t addr;
  volatile uint8_t * flags;
  char fname[FILENAME_MAX];

  /* Sanity check input values. */
  ASSERT(NULL != ate);
  ASSERT(num <= ate->n_pages);
  ASSERT(beg <= ate->n_pages-num);

  /* Default return value. */
  retval = 0;

  /* Shortcut if no pages in range. */
  if (0 == num)
    goto RETURN;
  /* Shortcut if there are no resident pages pages - must execute this even if
   * there are no dirty pages, since resident+clean pages must also be evicted.
   * */
  if (0 == ate->l_pages)
    goto RETURN;

  /* Setup local variables. */
  page_size = _vmm_.page_size;
  addr      = ate->base;
  flags     = ate->flags;
  end       = beg+num;

  /* Generate file name. */
  ret = snprintf(fname, FILENAME_MAX, "%s%d-%zx", _vmm_.fstem, (int)getpid(),\
    (uintptr_t)ate);
  ERRCHK(ERREXIT, 0 > ret);
  /* Open the file for writing. */
  fd = libc_open(fname, O_WRONLY);
  ERRCHK(ERREXIT, -1 == fd);

  /* Go over the pages and write the ones that have changed. Perform the writes
   * in contigous chunks of changed pages. */
  for (ipfirst=-1,ip=beg; ip<=end; ++ip) {
    if (ip != end && (MMU_DIRTY != (flags[ip]&MMU_DIRTY))) {
      if (MMU_CHRGD != (flags[ip]&MMU_CHRGD)) {   /* is charged */
        if (MMU_RSDNT != (flags[ip]&MMU_RSDNT)) { /* is resident */
          ASSERT(ate->l_pages > 0);
          ate->l_pages--;
        }

        ASSERT(ate->c_pages > 0);
        ate->c_pages--;
      }

      /* flag: 101* */
      flags[ip] &= MMU_ZFILL;
      flags[ip] |= (MMU_CHRGD|MMU_RSDNT);
    }

    if (ip != end && (MMU_DIRTY == (flags[ip]&MMU_DIRTY))) {
      if (-1 == ipfirst)
        ipfirst = ip;

      ASSERT(MMU_RSDNT != (flags[ip]&MMU_RSDNT)); /* is resident */
      ASSERT(MMU_CHRGD != (flags[ip]&MMU_CHRGD)); /* is charged */

      ASSERT(ate->l_pages > 0);
      ate->l_pages--;
      ASSERT(ate->c_pages > 0);
      ate->c_pages--;

      /* flag: 1011 */
      flags[ip] = (MMU_CHRGD|MMU_RSDNT|MMU_ZFILL);
    }
    else if (-1 != ipfirst) {
      ret = vmm_write(fd, (void*)(addr+(ipfirst*page_size)),\
        (ip-ipfirst)*page_size, ipfirst*page_size);
      ERRCHK(ERREXIT, -1 == ret);

      numwr += (ip-ipfirst);

      ASSERT(ate->d_pages >= ip-ipfirst);
      ate->d_pages -= (ip-ipfirst);

      ipfirst = -1;
    }
  }

  /* close file */
  ret = close(fd);
  ERRCHK(ERREXIT, -1 == ret);

  if (VMM_MLOCK == (_vmm_.opts&VMM_MLOCK)) {
    /* unlock the memory from RAM */
    ret = munlock((void*)(addr+(beg*page_size)), num*page_size);
    ERRCHK(ERREXIT, -1 == ret);
  }

  /* update its protection to none */
  ret = mprotect((void*)(addr+(beg*page_size)), num*page_size, PROT_NONE);
  ERRCHK(ERREXIT, -1 == ret);

  /* unlock the memory, update its protection to none and advise kernel to
   * release its associated resources */
  ret = madvise((void*)(addr+(beg*page_size)), num*page_size,\
    MADV_DONTNEED);
  ERRCHK(ERREXIT, -1 == ret);

  /***************************************************************************/
  /* Successful exit -- return numwr. */
  /***************************************************************************/
  retval = numwr;
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
