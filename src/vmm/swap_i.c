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


#include <fcntl.h>    /* O_RDWR, O_CREAT, O_EXCL */
#include <stddef.h>   /* NULL, size_t */
#include <stdint.h>   /* uint8_t, uintptr_t */
#include <stdio.h>    /* FILENAME_MAX */
#include <string.h>   /* snprintf */
#include <sys/mman.h> /* mmap, mremap, mprotect */
#include "common.h"
#include "sbma.h"
#include "vmm.h"


/*****************************************************************************/
/*  Read data from file.                                                     */
/*                                                                           */
/*  MT-Unsafe race:buf                                                       */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  Call only when thread is in possession of ate->lock which          */
/*        corresponds to the buffer in question.                             */
/*****************************************************************************/
SBMA_STATIC int
vmm_read(int const fd, void * const buf, size_t len, size_t off)
{
  ssize_t len_;
  char * buf_ = (char*)buf;

#ifndef HAVE_PREAD
  if (-1 == lseek(fd, off, SEEK_SET))
    return -1;
#endif

  do {
#ifdef HAVE_PREAD
    if (-1 == (len_=libc_pread(fd, buf_, len, off)))
      return -1;
    off += len_;
#else
    if (-1 == (len_=libc_read(fd, buf_, len)))
      return -1;
#endif

    ASSERT(0 != len_);

    buf_ += len_;
    len -= len_;
  } while (len > 0);

  return 0;
}


/*****************************************************************************/
/*  Read pages without zfill flag from file and update their memory          */
/*  protections.                                                             */
/*                                                                           */
/*  MT-Unsafe race:ate->*                                                    */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  Call only when thread is in possession of ate->lock.               */
/*****************************************************************************/
SBMA_EXTERN ssize_t
vmm_swap_i(struct ate * const ate, size_t const beg, size_t const num,
           int const ghost)
{
  int retval, ret, fd;
  size_t ip, page_size, end, numrd=0;
  ssize_t ipfirst;
  uintptr_t addr, raddr;
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
  /* Shortcut if all pages are already loaded. */
  if (ate->l_pages == ate->n_pages) {
    ASSERT(ate->c_pages == ate->n_pages);
    goto RETURN;
  }

  /* Setup local variables. */
  page_size = _vmm_.page_size;
  flags     = ate->flags;
  end       = beg+num;

  if (VMM_GHOST == ghost) {
    /* mmap temporary memory with write protection for loading from disk. */
    addr = (uintptr_t)mmap(NULL, num*page_size, PROT_WRITE, SBMA_MMAP_FLAG,\
      -1, 0);
    ERRCHK(ERREXIT, (uintptr_t)MAP_FAILED == addr);
  }
  else {
    addr = ate->base+(beg*page_size);
    ret  = mprotect((void*)addr, num*page_size, PROT_WRITE);
    ERRCHK(ERREXIT, -1 == ret);
  }

  /* Compute file name. */
  ret = snprintf(fname, FILENAME_MAX, "%s%d-%zx", _vmm_.fstem, (int)getpid(),\
    (uintptr_t)ate);
  ERRCHK(ERREXIT, 0 > ret);
  /* Open the file for reading. */
  fd = libc_open(fname, O_RDONLY);
  ERRCHK(ERREXIT, -1 == fd);

  /* Load only those pages which were previously written to disk and have
   * not since been dumped. */
  for (ipfirst=-1,ip=beg; ip<=end; ++ip) {
    if (ip != end &&\
        (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) &&  /* not resident */\
        (MMU_ZFILL == (flags[ip]&MMU_ZFILL)) &&  /* cannot be zero filled */\
        (MMU_DIRTY != (flags[ip]&MMU_DIRTY)))    /* not dirty */
    {
      if (-1 == ipfirst)
        ipfirst = ip;
    }
    else if (-1 != ipfirst) {
      ret = vmm_read(fd, (void*)(addr+((ipfirst-beg)*page_size)),
        (ip-ipfirst)*page_size, ipfirst*page_size);
      ERRCHK(ERREXIT, -1 == ret);

      if (VMM_GHOST == ghost) {
        /* Give read permission to temporary pages. */
        ret = mprotect((void*)(addr+((ipfirst-beg)*page_size)),\
          (ip-ipfirst)*page_size, PROT_READ);
        ERRCHK(ERREXIT, -1 == ret);

        /* mremap temporary pages into persistent memory. */
        raddr = (uintptr_t)mremap((void*)(addr+((ipfirst-beg)*page_size)),\
          (ip-ipfirst)*page_size, (ip-ipfirst)*page_size,\
          MREMAP_MAYMOVE|MREMAP_FIXED,\
          (void*)(ate->base+(ipfirst*page_size)));
        ERRCHK(ERREXIT, MAP_FAILED == (void*)raddr);
      }

      numrd += (ip-ipfirst);

      ipfirst = -1;
    }

    if (ip != end) {
      if (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) { /* not resident */
        ASSERT(ate->l_pages < ate->n_pages);
        ate->l_pages++;

        if (MMU_CHRGD == (flags[ip]&MMU_CHRGD)) { /* not charged */
          ASSERT(ate->c_pages < ate->n_pages);
          ate->c_pages++;
        }

        /* flag: 0*0* */
        flags[ip] &= ~(MMU_CHRGD|MMU_RSDNT);
      }
      else {
        ASSERT(MMU_CHRGD != (flags[ip]&MMU_CHRGD)); /* is charged */
      }
    }
  }

  /* Close file. */
  ret = close(fd);
  ERRCHK(ERREXIT, -1 == ret);

  if (VMM_GHOST == ghost) {
    /* munmap any remaining temporary pages. */
    ret = munmap((void*)addr, num*page_size);
    ERRCHK(ERREXIT, -1 == ret);
  }
  else {
    /* Update protection of temporary mapping to read-only. */
    ret = mprotect((void*)addr, num*page_size, PROT_READ);
    ERRCHK(ERREXIT, -1 == ret);

    /* Update protection of temporary mapping and copy data for any dirty
     * pages. */
    for (ip=beg; ip<end; ++ip) {
      if (MMU_DIRTY == (flags[ip]&MMU_DIRTY)) {
        ret = mprotect((void*)(addr+((ip-beg)*page_size)), page_size,\
          PROT_READ|PROT_WRITE);
        ERRCHK(ERREXIT, -1 == ret);
      }
    }
  }

  /***************************************************************************/
  /* Successful exit -- return numrd. */
  /***************************************************************************/
  retval = numrd;
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
