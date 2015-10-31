/*
Copyright (c) 2015 Jeremy Iverson

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


#include <errno.h>    /* errno library */
#include <fcntl.h>    /* O_RDWR, O_CREAT, O_EXCL */
#include <signal.h>   /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stddef.h>   /* NULL, size_t */
#include <stdint.h>   /* uint8_t, uintptr_t */
#include <stdio.h>    /* FILENAME_MAX */
#include <string.h>   /* strncpy */
#include <sys/mman.h> /* mmap, mremap, madvise, mprotect */
#include <time.h>     /* struct timespec */
#include "common.h"
#include "ipc.h"
#include "lock.h"
#include "mmu.h"
#include "sbma.h"
#include "vmm.h"


/*****************************************************************************/
/*  Required function prototypes. */
/*****************************************************************************/
int __sbma_mevictall_int(size_t * const, size_t * const, size_t * const);


/*****************************************************************************/
/*  Read data from file.                                                     */
/*                                                                           */
/*  MT-Safe                                                                  */
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
/*  Write data to file.                                                      */
/*                                                                           */
/*  MT-Safe                                                                  */
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
/*  SIGSEGV handler.                                                         */
/*                                                                           */
/*  MT-Safe                                                                  */
/*****************************************************************************/
SBMA_STATIC void
vmm_sigsegv(int const sig, siginfo_t * const si, void * const ctx)
{
  int ret;
  size_t ip, page_size, _len;
  uintptr_t addr;
  void * _addr;
  volatile uint8_t * flags;
  struct ate * ate;

  /* make sure we received a SIGSEGV */
  ASSERT(SIGSEGV == sig);

  /* setup local variables */
  page_size = vmm.page_size;
  addr      = (uintptr_t)si->si_addr;

  /* lookup allocation table entry */
  ate = __mmu_lookup_ate(&(vmm.mmu), (void*)addr);
  ASSERT((struct ate*)-1 != ate);
  ASSERT(NULL != ate);

  ip    = (addr-ate->base)/page_size;
  flags = ate->flags;

  if (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) {
    if (VMM_LZYRD == (vmm.opts&VMM_LZYRD)) {
      _addr = (void*)(ate->base+ip*page_size);
      _len  = page_size;
    }
    else {
      _addr = (void*)ate->base;
      _len  = ate->n_pages*page_size;
    }

    ret = __sbma_mtouch(ate, _addr, _len);
    ASSERT(-1 != ret);

    ret = __lock_let(&(ate->lock));
    ASSERT(-1 != ret);

    VMM_INTRA_CRITICAL_SECTION_BEG(&vmm);
    VMM_TRACK(&vmm, numrf, 1);
    VMM_INTRA_CRITICAL_SECTION_END(&vmm);
  }
  else {
    /* sanity check */
    ASSERT(MMU_DIRTY != (flags[ip]&MMU_DIRTY)); /* not dirty */

    /* flag: 100 */
    flags[ip] = MMU_DIRTY;

    /* update protection to read-write */
    ret = mprotect((void*)(ate->base+(ip*page_size)), page_size,\
      PROT_READ|PROT_WRITE);
    ASSERT(-1 != ret);

    /* release lock on alloction table entry */
    ret = __lock_let(&(ate->lock));
    ASSERT(-1 != ret);

    /* increase count of dirty pages */
    ate->d_pages++;
    ret = ipc_mdirty(&(vmm.ipc), VMM_TO_SYS(1));
    ASSERT(-1 != ret);

    VMM_INTRA_CRITICAL_SECTION_BEG(&vmm);
    VMM_TRACK(&vmm, numwf, 1);
    VMM_INTRA_CRITICAL_SECTION_END(&vmm);
  }

  if (NULL == ctx) {} /* suppress unused warning */
}


/*****************************************************************************/
/*  SIGIPC handler.                                                          */
/*                                                                           */
/*  MT-Safe                                                                  */
/*****************************************************************************/
SBMA_STATIC void
vmm_sigipc(int const sig, siginfo_t * const si, void * const ctx)
{
  int ret;
  size_t c_pages, d_pages, numwr;
  struct timespec tmr;

  /* Sanity check: make sure we received a SIGIPC. */
  ASSERT(SIGIPC <= SIGRTMAX);
  ASSERT(SIGIPC == sig);

  /* Only honor the SIGIPC if my status is still eligible. */
  if (ipc_is_eligible(&(vmm.ipc), vmm.ipc.id)) {
    /* XXX Is it possible / what happens / does it matter if the process
     * receives a SIGIPC at this point in the execution of the signal handler?
     * */
    /* XXX This is not possible according to signal.h man pages. There it
     * states that ``In addition, the signal which triggered the handler will
     * be blocked, unless the SA_NODEFER flag is used.''. In the current code,
     * SA_NODEFER is not used. */

    /*=======================================================================*/
    TIMER_START(&(tmr));
    /*=======================================================================*/

    /* Evict all memory. */
    ret = __sbma_mevictall_int(&c_pages, &d_pages, &numwr);
    ASSERT(-1 != ret);

    /* Update ipc memory statistics. */
    ipc_atomic_dec(&(vmm.ipc), c_pages, d_pages);

    /*=======================================================================*/
    TIMER_STOP(&(tmr));
    /*=======================================================================*/

    /* Track number of number of syspages written to disk, time taken for
     * writing, and number of SIGIPC honored. */
    VMM_INTRA_CRITICAL_SECTION_BEG(&vmm);
    VMM_TRACK(&vmm, numwr, numwr);
    VMM_TRACK(&vmm, tmrwr, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);
    VMM_TRACK(&vmm, numhipc, 1);
    VMM_INTRA_CRITICAL_SECTION_END(&vmm);
  }

  /* Signal to the waiting process that the memory has been released. */
  ret = sem_post(vmm.ipc.done);
  ASSERT(-1 != ret);

  /* Track the number of SIGIPC received. */
  VMM_INTRA_CRITICAL_SECTION_BEG(&vmm);
  VMM_TRACK(&vmm, numipc, 1);
  VMM_INTRA_CRITICAL_SECTION_END(&vmm);

  if (NULL == si || NULL == ctx) {}
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
  page_size = vmm.page_size;
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
  ret = snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
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
SBMA_EXPORT(internal, ssize_t
vmm_swap_i(struct ate * const ate, size_t const beg, size_t const num,
           int const ghost));


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
  /* Shortcut if there are no dirty pages. */
  if (0 == ate->d_pages)
    goto RETURN;

  /* Setup local variables. */
  page_size = vmm.page_size;
  addr      = ate->base;
  flags     = ate->flags;
  end       = beg+num;

  /* Generate file name. */
  ret = snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
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

  if (VMM_MLOCK == (vmm.opts&VMM_MLOCK)) {
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
SBMA_EXPORT(internal, ssize_t
vmm_swap_o(struct ate * const ate, size_t const beg, size_t const num));


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
  /* Shortcut if there are no dirty pages. */
  if (0 == ate->d_pages)
    goto RETURN;

  /* Setup local variables. */
  page_size = vmm.page_size;
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
SBMA_EXPORT(internal, ssize_t
vmm_swap_x(struct ate * const ate, size_t const beg, size_t const num));


SBMA_EXTERN int
vmm_init(struct vmm * const vmm, char const * const fstem, int const uniq,
         size_t const page_size, int const n_procs, size_t const max_mem,
         int const opts)
{
  int retval;

  /* Default return value. */
  retval = 0;

  /* Shortcut if vmm is already initialized. */
  if (1 == vmm->init)
    goto RETURN;
  /* Shortcut if opts are invalid. */
  if (VMM_INVLD == (opts&VMM_INVLD))
    goto ERREXIT;

  /* Set page size. */
  vmm->page_size = page_size;

  /* Set options. */
  vmm->opts = opts;

  /* Initialize statistics. */
  vmm->numipc   = 0;
  vmm->numhipc  = 0;
  vmm->numrf    = 0;
  vmm->numwf    = 0;
  vmm->numrd    = 0;
  vmm->numwr    = 0;
  vmm->tmrrd    = 0.0;
  vmm->tmrwr    = 0.0;
  vmm->numpages = 0;

  /* Copy file stem. */
  strncpy(vmm->fstem, fstem, FILENAME_MAX-1);
  vmm->fstem[FILENAME_MAX-1] = '\0';

  /* Setup the signal handler for SIGSEGV. */
  vmm->act_segv.sa_flags     = SA_SIGINFO;
  vmm->act_segv.sa_sigaction = vmm_sigsegv;
  retval = sigemptyset(&(vmm->act_segv.sa_mask));
  ERRCHK(FATAL, -1 == retval);
  retval = sigaction(SIGSEGV, &(vmm->act_segv), &(vmm->oldact_segv));
  ERRCHK(FATAL, -1 == retval);

  /* Setup the signal handler for SIGIPC. */
  vmm->act_ipc.sa_flags     = SA_SIGINFO;
  vmm->act_ipc.sa_sigaction = vmm_sigipc;
  retval = sigemptyset(&(vmm->act_ipc.sa_mask));
  ERRCHK(FATAL, -1 == retval);
  retval = sigaction(SIGIPC, &(vmm->act_ipc), &(vmm->oldact_ipc));
  ERRCHK(FATAL, -1 == retval);

  /* Initialize mmu. */
  retval = __mmu_init(&(vmm->mmu), page_size);
  ERRCHK(FATAL, -1 == retval);

  /* Initialize ipc. */
  retval = ipc_init(&(vmm->ipc), uniq, n_procs, max_mem);
  ERRCHK(FATAL, -1 == retval);

  /* Initialize vmm lock. */
  retval = __lock_init(&(vmm->lock));
  ERRCHK(FATAL, -1 == retval);

  vmm->init = 1;

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

  /***************************************************************************/
  /* Fatal error -- an unrecoverable error has occured, the runtime state
   * cannot be reverted to its state before this function was called. */
  /***************************************************************************/
  FATAL:
  FATAL_ABORT(errno);
}
SBMA_EXPORT(internal, int
vmm_init(struct vmm * const vmm, char const * const fstem, int const uniq,
         size_t const page_size, int const n_procs, size_t const max_mem,
         int const opts));


SBMA_EXTERN int
vmm_destroy(struct vmm * const vmm)
{
  int retval;

  /* Default return value. */
  retval = 0;

  /* Shortcut if vmm is not initialized. */
  if (0 == vmm->init)
    goto RETURN;

  vmm->init = 0;

  /* reset signal handler for SIGSEGV */
  retval = sigaction(SIGSEGV, &(vmm->oldact_segv), NULL);
  ERRCHK(FATAL, -1 == retval);

  /* reset signal handler for SIGIPC */
  retval = sigaction(SIGIPC, &(vmm->oldact_ipc), NULL);
  ERRCHK(FATAL, -1 == retval);

  /* destroy mmu */
  retval = __mmu_destroy(&(vmm->mmu));
  ERRCHK(RETURN, 0 != retval);

  /* destroy ipc */
  retval = ipc_destroy(&(vmm->ipc));
  ERRCHK(RETURN, 0 != retval);

  /* destroy vmm lock */
  retval = __lock_free(&(vmm->lock));
  ERRCHK(RETURN, 0 != retval);

  /***************************************************************************/
  /* Successful exit -- return 0. */
  /***************************************************************************/
  goto RETURN;

  /***************************************************************************/
  /* Return point -- return. */
  /***************************************************************************/
  RETURN:
  return retval;

  /***************************************************************************/
  /* Fatal error -- an unrecoverable error has occured, the runtime state
   * cannot be reverted to its state before this function was called. */
  /***************************************************************************/
  FATAL:
  FATAL_ABORT(errno);
}
SBMA_EXPORT(internal, int
vmm_destroy(struct vmm * const vmm));
