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


#include <errno.h>    /* errno library */
#include <fcntl.h>    /* O_RDWR, O_CREAT, O_EXCL */
#include <signal.h>   /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stddef.h>   /* NULL, size_t */
#include <stdint.h>   /* uint8_t, uintptr_t */
#include <stdio.h>    /* FILENAME_MAX */
#include <string.h>   /* strncpy */
#include <sys/mman.h> /* mmap, mremap, madvise, mprotect */
#include <time.h>     /* struct timespec */
#include <unistd.h>   /* sysconf */
#include "common.h"
#include "ipc.h"
#include "lock.h"
#include "mmu.h"
#include "sbma.h"
#include "vmm.h"


/****************************************************************************/
/*! Required function prototypes. */
/****************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

int __sbma_mevictall_int(size_t * const, size_t * const, size_t * const);

#ifdef __cplusplus
}
#endif


/****************************************************************************/
/*! Read data from file. */
/****************************************************************************/
SBMA_STATIC int
__vmm_read(int const __fd, void * const __buf, size_t __len, size_t __off)
{
  ssize_t len;
  char * buf = (char*)__buf;

#ifndef HAVE_PREAD
  if (-1 == lseek(__fd, __off, SEEK_SET))
    return -1;
#endif

  do {
#ifdef HAVE_PREAD
    if (-1 == (len=libc_pread(__fd, buf, __len, __off)))
      return -1;
    __off += len;
#else
    if (-1 == (len=libc_read(__fd, buf, __len)))
      return -1;
#endif

    ASSERT(0 != len);

    buf += len;
    __len -= len;
  } while (__len > 0);

  return 0;
}


/****************************************************************************/
/*! Write data to file. */
/****************************************************************************/
SBMA_STATIC int
__vmm_write(int const __fd, void const * const __buf, size_t __len,
            size_t __off)
{
  ssize_t len;
  char * buf = (char*)__buf;

#ifndef HAVE_PWRITE
  if (-1 == lseek(__fd, __off, SEEK_SET))
    return -1;
#endif

  do {
#ifdef HAVE_PWRITE
    if (-1 == (len=libc_pwrite(__fd, buf, __len, __off)))
      return -1;
    __off += len;
#else
    if (-1 == (len=libc_write(__fd, buf, __len)))
      return -1;
#endif

    ASSERT(0 != len);

    buf += len;
    __len -= len;
  } while (__len > 0);

  return 0;
}


/****************************************************************************/
/*! SIGSEGV handler. */
/****************************************************************************/
SBMA_STATIC void
__vmm_sigsegv(int const sig, siginfo_t * const si, void * const ctx)
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

    VMM_TRACK(numrf, 1);
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

    VMM_TRACK(numwf, 1);
  }

  if (NULL == ctx) {} /* suppress unused warning */
}


/****************************************************************************/
/*! SIGIPC handler. */
/****************************************************************************/
SBMA_STATIC void
__vmm_sigipc(int const sig, siginfo_t * const si, void * const ctx)
{
  int ret;
  size_t c_pages, d_pages, numwr;
  struct timespec tmr;

  /* make sure we received a SIGIPC */
  ASSERT(SIGIPC <= SIGRTMAX);
  ASSERT(SIGIPC == sig);

  /* Only honor the SIGIPC if my status is still eligible */
  if (ipc_is_eligible(&(vmm.ipc), vmm.ipc.id)) {
    /* XXX Is it possible / what happens / does it matter if the process
     * receives a SIGIPC at this point in the execution of the signal handler?
     * */
    /* XXX This is not possible according to signal.h man pages. There it
     * states that ``In addition, the signal which triggered the handler will
     * be blocked, unless the SA_NODEFER flag is used.''. In the current code,
     * SA_NODEFER is not used. */

    /*======================================================================*/
    TIMER_START(&(tmr));
    /*======================================================================*/

    /* evict all memory */
    ret = __sbma_mevictall_int(&c_pages, &d_pages, &numwr);
    ASSERT(-1 != ret);

    /* update ipc memory statistics */
    ipc_atomic_dec(&(vmm.ipc), c_pages, d_pages);

    /*======================================================================*/
    TIMER_STOP(&(tmr));
    /*======================================================================*/

    /* track number of syspages currently loaded, number of syspages written
     * to disk, and high water mark for syspages loaded */
    VMM_TRACK(numwr, numwr);
    VMM_TRACK(tmrwr, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);
    VMM_TRACK(numhipc, 1);
  }

  /* signal to the waiting process that the memory has been released */
  ret = sem_post(vmm.ipc.done);
  ASSERT(-1 != ret);

  VMM_TRACK(numipc, 1);

  if (NULL == si || NULL == ctx) {}
}


SBMA_EXTERN ssize_t
__vmm_swap_i(struct ate * const __ate, size_t const __beg, size_t const __num,
             int const __ghost)
{
  int ret, fd;
  size_t ip, page_size, end, numrd=0;
  ssize_t ipfirst;
  uintptr_t addr, raddr;
  volatile uint8_t * flags;
  char fname[FILENAME_MAX];

  /* error check input values */
  ASSERT(NULL != __ate);
  ASSERT(__num <= __ate->n_pages);
  ASSERT(__beg <= __ate->n_pages-__num);

  /* shortcut */
  if (0 == __num)
    return 0;

  /* shortcut by checking to see if all pages are already loaded */
  if (__ate->l_pages == __ate->n_pages) {
    ASSERT(__ate->c_pages == __ate->n_pages);
    return 0;
  }

  /* setup local variables */
  page_size = vmm.page_size;
  flags     = __ate->flags;
  end       = __beg+__num;

  if (VMM_GHOST == __ghost) {
    /* mmap temporary memory with write protection for loading from disk */
    addr = (uintptr_t)mmap(NULL, __num*page_size, PROT_WRITE, SBMA_MMAP_FLAG,\
      -1, 0);
    if ((uintptr_t)MAP_FAILED == addr)
      goto ERREXIT;
  }
  else {
    addr = __ate->base+(__beg*page_size);
    ret  = mprotect((void*)addr, __num*page_size, PROT_WRITE);
    if (-1 == ret)
      goto ERREXIT;
  }

  /* compute file name */
  ret = snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    (uintptr_t)__ate);
  if (0 > ret)
    goto ERREXIT;
  /* open the file for reading */
  fd = libc_open(fname, O_RDONLY);
  if (-1 == fd)
    goto ERREXIT;

  /* load only those pages which were previously written to disk and have
   * not since been dumped */
  for (ipfirst=-1,ip=__beg; ip<=end; ++ip) {
    if (ip != end &&\
        (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) &&  /* not resident */\
        (MMU_ZFILL == (flags[ip]&MMU_ZFILL)) &&  /* cannot be zero filled */\
        (MMU_DIRTY != (flags[ip]&MMU_DIRTY)))    /* not dirty */
    {
      if (-1 == ipfirst)
        ipfirst = ip;
    }
    else if (-1 != ipfirst) {
      ret = __vmm_read(fd, (void*)(addr+((ipfirst-__beg)*page_size)),
        (ip-ipfirst)*page_size, ipfirst*page_size);
      if (-1 == ret)
        goto ERREXIT;

      if (VMM_GHOST == __ghost) {
        /* give read permission to temporary pages */
        ret = mprotect((void*)(addr+((ipfirst-__beg)*page_size)),\
          (ip-ipfirst)*page_size, PROT_READ);
        if (-1 == ret)
          goto ERREXIT;
        /* remap temporary pages into persistent memory */
        raddr = (uintptr_t)mremap((void*)(addr+((ipfirst-__beg)*page_size)),\
          (ip-ipfirst)*page_size, (ip-ipfirst)*page_size,\
          MREMAP_MAYMOVE|MREMAP_FIXED,\
          (void*)(__ate->base+(ipfirst*page_size)));
        if (MAP_FAILED == (void*)raddr)
          goto ERREXIT;
      }

      numrd += (ip-ipfirst);

      ipfirst = -1;
    }

    if (ip != end) {
      if (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) { /* not resident */
        ASSERT(__ate->l_pages < __ate->n_pages);
        __ate->l_pages++;

        if (MMU_CHRGD == (flags[ip]&MMU_CHRGD)) { /* not charged */
          ASSERT(__ate->c_pages < __ate->n_pages);
          __ate->c_pages++;
        }

        /* flag: 0*0* */
        flags[ip] &= ~(MMU_CHRGD|MMU_RSDNT);
      }
      else {
        ASSERT(MMU_CHRGD != (flags[ip]&MMU_CHRGD)); /* is charged */
      }
    }
  }

  /* close file */
  ret = close(fd);
  if (-1 == ret)
    goto ERREXIT;

  if (VMM_GHOST == __ghost) {
    /* unmap any remaining temporary pages */
    ret = munmap((void*)addr, __num*page_size);
    if (-1 == ret)
      goto ERREXIT;
  }
  else {
    /* update protection of temporary mapping to read-only */
    ret = mprotect((void*)addr, __num*page_size, PROT_READ);
    if (-1 == ret)
      goto ERREXIT;

    /* update protection of temporary mapping and copy data for any dirty pages
     * */
    for (ip=__beg; ip<end; ++ip) {
      if (MMU_DIRTY == (flags[ip]&MMU_DIRTY)) {
        ret = mprotect((void*)(addr+((ip-__beg)*page_size)), page_size,\
          PROT_READ|PROT_WRITE);
        if (-1 == ret)
          goto ERREXIT;
      }
    }
  }

  /* return the number of pages read from disk */
  return numrd;

  ERREXIT:
  return -1;
}
SBMA_EXPORT(internal, ssize_t
__vmm_swap_i(struct ate * const __ate, size_t const __beg, size_t const __num,
             int const __ghost));


SBMA_EXTERN ssize_t
__vmm_swap_o(struct ate * const __ate, size_t const __beg, size_t const __num)
{
  int ret, fd;
  size_t ip, page_size, end, numwr=0;
  ssize_t ipfirst;
  uintptr_t addr;
  volatile uint8_t * flags;
  char fname[FILENAME_MAX];

  /* error check input values */
  ASSERT(NULL != __ate);
  ASSERT(__num <= __ate->n_pages);
  ASSERT(__beg <= __ate->n_pages-__num);

  /* shortcut */
  if (0 == __num)
    return 0;

  /* shortcut by checking to see if no pages are currently loaded */
  /* TODO: if we track the number of dirty pages, then this can do a better
   * job of short-cutting */
  if (0 == __ate->c_pages)
    return 0;

  /* setup local variables */
  page_size = vmm.page_size;
  addr      = __ate->base;
  flags     = __ate->flags;
  end       = __beg+__num;

  /* compute file name */
  ret = snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    (uintptr_t)__ate);
  if (0 > ret)
    goto ERREXIT;
  /* open the file for writing */
  fd = libc_open(fname, O_WRONLY);
  if (-1 == fd)
    goto ERREXIT;

  /* go over the pages and write the ones that have changed. perform the
   * writes in contigous chunks of changed pages. */
  for (ipfirst=-1,ip=__beg; ip<=end; ++ip) {
    if (ip != end && (MMU_DIRTY != (flags[ip]&MMU_DIRTY))) {
      if (MMU_CHRGD != (flags[ip]&MMU_CHRGD)) {   /* is charged */
        if (MMU_RSDNT != (flags[ip]&MMU_RSDNT)) { /* is resident */
          ASSERT(__ate->l_pages > 0);
          __ate->l_pages--;
        }

        ASSERT(__ate->c_pages > 0);
        __ate->c_pages--;
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

      ASSERT(__ate->l_pages > 0);
      __ate->l_pages--;
      ASSERT(__ate->c_pages > 0);
      __ate->c_pages--;

      /* flag: 1011 */
      flags[ip] = (MMU_CHRGD|MMU_RSDNT|MMU_ZFILL);
    }
    else if (-1 != ipfirst) {
      ret = __vmm_write(fd, (void*)(addr+(ipfirst*page_size)),\
        (ip-ipfirst)*page_size, ipfirst*page_size);
      if (-1 == ret)
        goto ERREXIT;

      numwr += (ip-ipfirst);

      ASSERT(__ate->d_pages >= ip-ipfirst);
      __ate->d_pages -= (ip-ipfirst);

      ipfirst = -1;
    }
  }

  /* close file */
  ret = close(fd);
  if (-1 == ret)
    goto ERREXIT;

  if (VMM_MLOCK == (vmm.opts&VMM_MLOCK)) {
    /* unlock the memory from RAM */
    ret = munlock((void*)(addr+(__beg*page_size)), __num*page_size);
    if (-1 == ret)
      goto ERREXIT;
  }

  /* update its protection to none */
  ret = mprotect((void*)(addr+(__beg*page_size)), __num*page_size, PROT_NONE);
  if (-1 == ret)
    goto ERREXIT;

  /* unlock the memory, update its protection to none and advise kernel to
   * release its associated resources */
  ret = madvise((void*)(addr+(__beg*page_size)), __num*page_size,\
    MADV_DONTNEED);
  if (-1 == ret)
    goto ERREXIT;

  /* return the number of pages written to disk */
  return numwr;

  ERREXIT:
  return -1;
}
SBMA_EXPORT(internal, ssize_t
__vmm_swap_o(struct ate * const __ate, size_t const __beg,
             size_t const __num));


SBMA_EXTERN ssize_t
__vmm_swap_x(struct ate * const __ate, size_t const __beg,
             size_t const __num)
{
  int ret;
  size_t ip, end, page_size;
  volatile uint8_t * flags;

  /* error check input values */
  ASSERT(NULL != __ate);
  ASSERT(__num <= __ate->n_pages);
  ASSERT(__beg <= __ate->n_pages-__num);

  /* shortcut */
  if (0 == __num)
    return 0;

  /* shortcut by checking to see if no pages are currently loaded */
  /* TODO: if we track the number of dirty pages, then this can do a better
   * job of short-cutting */
  if (0 == __ate->l_pages)
    return 0;

  /* setup local variables */
  page_size = vmm.page_size;
  flags     = __ate->flags;
  end       = __beg+__num;

  for (ip=__beg; ip<end; ++ip) {
    if (MMU_DIRTY == (flags[ip]&MMU_DIRTY)) {
      ret = mprotect((void*)(__ate->base+(ip*page_size)), page_size,\
        PROT_READ);
      if (-1 == ret)
        return -1;

      ASSERT(__ate->d_pages > 0);
      __ate->d_pages--;
    }
    /* flag: *0*0 */
    flags[ip] &= ~(MMU_DIRTY|MMU_ZFILL);
  }

  return 0;
}
SBMA_EXPORT(internal, ssize_t
__vmm_swap_x(struct ate * const __ate, size_t const __beg,
             size_t const __num));


SBMA_EXTERN int
__vmm_init(struct vmm * const __vmm, char const * const __fstem,
           int const __uniq, size_t const __page_size, int const __n_procs,
           size_t const __max_mem, int const __opts)
{
  if (1 == __vmm->init)
    return 0;
  if (VMM_INVLD == (__opts&VMM_INVLD))
    return -1;

  /* set page size */
  __vmm->page_size = __page_size;

  /* set options */
  __vmm->opts = __opts;

  /* initialize statistics */
  __vmm->numipc   = 0;
  __vmm->numhipc  = 0;
  __vmm->numrf    = 0;
  __vmm->numwf    = 0;
  __vmm->numrd    = 0;
  __vmm->numwr    = 0;
  __vmm->tmrrd    = 0.0;
  __vmm->tmrwr    = 0.0;
  __vmm->numpages = 0;

  /* copy file stem */
  strncpy(__vmm->fstem, __fstem, FILENAME_MAX-1);
  __vmm->fstem[FILENAME_MAX-1] = '\0';

  /* setup the signal handler for SIGSEGV */
  __vmm->act_segv.sa_flags     = SA_SIGINFO;
  __vmm->act_segv.sa_sigaction = __vmm_sigsegv;
  if (-1 == sigemptyset(&(__vmm->act_segv.sa_mask)))
    return -1;
  if (-1 == sigaction(SIGSEGV, &(__vmm->act_segv), &(__vmm->oldact_segv)))
    return -1;

  /* setup the signal handler for SIGIPC */
  __vmm->act_ipc.sa_flags     = SA_SIGINFO;
  __vmm->act_ipc.sa_sigaction = __vmm_sigipc;
  if (-1 == sigemptyset(&(__vmm->act_ipc.sa_mask)))
    return -1;
  if (-1 == sigaction(SIGIPC, &(__vmm->act_ipc), &(__vmm->oldact_ipc)))
    return -1;

  /* initialize mmu */
  if (-1 == __mmu_init(&(__vmm->mmu), __page_size))
    return -1;

  /* initialize ipc */
  if (-1 == ipc_init(&(__vmm->ipc), __uniq, __n_procs, __max_mem))
    return -1;

  /* initialize vmm lock */
  if (-1 == __lock_init(&(__vmm->lock)))
    return -1;

  vmm.init = 1;

  return 0;
}
SBMA_EXPORT(internal, int
__vmm_init(struct vmm * const __vmm, char const * const __fstem,
           int const __uniq, size_t const __page_size, int const __n_procs,
           size_t const __max_mem, int const __opts));


SBMA_EXTERN int
__vmm_destroy(struct vmm * const __vmm)
{
  int retval;

  if (0 == vmm.init)
    return 0;

  vmm.init = 0;

  /* reset signal handler for SIGSEGV */
  retval = sigaction(SIGSEGV, &(__vmm->oldact_segv), NULL);
  ERRCHK(FATAL, -1 == retval);

  /* reset signal handler for SIGIPC */
  retval = sigaction(SIGIPC, &(__vmm->oldact_ipc), NULL);
  ERRCHK(FATAL, -1 == retval);

  /* destroy mmu */
  retval = __mmu_destroy(&(__vmm->mmu));
  ERRCHK(RETURN, 0 != retval);

  /* destroy ipc */
  if (-1 == ipc_destroy(&(__vmm->ipc)))
    return -1;

  /* destroy vmm lock */
  retval = __lock_free(&(__vmm->lock));
  ERRCHK(RETURN, 0 != retval);

  /**************************************************************************/
  /* Successful exit -- return 0. */
  /**************************************************************************/
  goto RETURN;

  /**************************************************************************/
  /* Return point -- return. */
  /**************************************************************************/
  RETURN:
  return retval;

  /**************************************************************************/
  /* Fatal error -- an unrecoverable error has occured, the runtime state
   * cannot be reverted to its state before this function was called. */
  /**************************************************************************/
  FATAL:
  FATAL_ABORT(errno);
}
SBMA_EXPORT(internal, int
__vmm_destroy(struct vmm * const __vmm));
