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
#include <unistd.h>   /* sysconf */
#include "config.h"
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

int __sbma_mevictall_int(size_t * const, size_t * const);

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
  size_t ip, page_size, l_pages, chk_l_pages;
  ssize_t numrd;
  uintptr_t addr;
  volatile uint8_t * flags;
  struct ate * ate;

  /* make sure we received a SIGSEGV */
  ASSERT(SIGSEGV == sig);

  ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);

  /* setup local variables */
  page_size = vmm.page_size;
  addr      = (uintptr_t)si->si_addr;

  /* lookup allocation table entry */
  ate = __mmu_lookup_ate(&(vmm.mmu), (void*)addr);
  ASSERT(NULL != ate);

  ip    = (addr-ate->base)/page_size;
  flags = ate->flags;

  if (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) {
    for (;;) {
      if (VMM_LZYRD == (vmm.opts&VMM_LZYRD))
        l_pages = 1;
      else
        l_pages = ate->n_pages-ate->l_pages;

      /* TODO: in current code, each read fault requires a check with the ipc
       * memory tracking to ensure there is enough free space. However, in
       * previous versions of the SIGSEGV handler, this check was only done
       * once per allocation when lazy reading is enabled. On one hand, the
       * current implementation is better because it will result in a smaller
       * number of times that a process is asked to evict all of its memory.
       * On the other hand, it requires the acquisition of a mutex for every
       * read fault. */

      chk_l_pages = ate->l_pages;

      ret = __ipc_madmit(&(vmm.ipc), VMM_TO_SYS(l_pages));
      if (-1 == ret && EAGAIN != errno) {
        (void)__lock_let(&(ate->lock));
        ASSERT(0);
      }
      else if (-1 != ret) {
        break;
      }
    }

    /* swap in the required memory */
    if (VMM_LZYRD == (vmm.opts&VMM_LZYRD)) {
      numrd = __vmm_swap_i(ate, ip, 1, vmm.opts&VMM_GHOST);
      ASSERT(-1 != numrd);
    }
    else {
      numrd = __vmm_swap_i(ate, 0, ate->n_pages, vmm.opts&VMM_GHOST);
      ASSERT(-1 != numrd);
      ASSERT(ate->l_pages == ate->n_pages);
    }

    ASSERT(l_pages == ate->l_pages-chk_l_pages);

    /* release lock on alloction table entry */
    ret = __lock_let(&(ate->lock));
    ASSERT(-1 != ret);

    /* track number of read faults, syspages read from disk, syspages
     * currently loaded, and high water mark for syspages loaded */
    VMM_TRACK(numrf, 1);
    VMM_TRACK(numrd, numrd);
    VMM_TRACK(curpages, VMM_TO_SYS(l_pages));
    VMM_TRACK(maxpages,
      vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);
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

    /* track number of write faults */
    VMM_TRACK(numwf, 1);
  }

  ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);

  if (NULL == ctx) {} /* suppress unused warning */
}


/****************************************************************************/
/*! SIGIPC handler. */
/****************************************************************************/
SBMA_STATIC void
__vmm_sigipc(int const sig, siginfo_t * const si, void * const ctx)
{
  int ret;
  size_t l_pages, numwr;

  /* make sure we received a SIGIPC */
  ASSERT(SIGIPC <= SIGRTMAX);
  ASSERT(SIGIPC == sig);

  /* Only honor the SIGIPC if my status is still eligible */
  if (1 == __ipc_is_eligible(&(vmm.ipc))) {
    /* Not sure if this is necessarily true. */
    /*ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);*/

    /* TODO: is it possible / what happens / does it matter if the process
     * receives a SIGIPC at this point in the execution of the signal handler?
     * */
    /* It shouldn't be possible because if this process received SIGIPC, then
     * the process which signaled it should be waiting on vmm.ipc.trn1, and
     * thus no other processes can signal. */

    /* evict all memory */
    ret = __sbma_mevictall_int(&l_pages, &numwr);
    ASSERT(-1 != ret);

    /* update ipc memory statistics */
    *(vmm.ipc.smem)          += l_pages;
    vmm.ipc.pmem[vmm.ipc.id] -= l_pages;
    ret = libc_msync((void*)vmm.ipc.shm, IPC_LEN(vmm.ipc.n_procs), MS_SYNC);
    ASSERT(-1 != ret);

    /* Not sure if this is necessarily true. */
    /*ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]+l_pages);*/

    /* track number of syspages currently loaded, number of syspages written
     * to disk, and high water mark for syspages loaded */
    VMM_TRACK(curpages, -l_pages);
    VMM_TRACK(numwr, numwr);

    /* change my status to unpopulated - must be before any potential waiting,
     * since SIGIPC could be raised again then. */
    ret = __ipc_unpopulate(&(vmm.ipc));
    ASSERT(-1 != ret);

    /* Not sure if this is necessarily true. */
    /*ASSERT(vmm.curpages == vmm.ipc.pmem[vmm.ipc.id]);*/
  }

  /* signal to the waiting process that the memory has been released */
  ret = sem_post(vmm.ipc.trn1);
  ASSERT(-1 != ret);

  VMM_TRACK(numipc, 1);

  if (NULL == si || NULL == ctx) {}
}


SBMA_EXTERN ssize_t
__vmm_swap_i(struct ate * const __ate, size_t const __beg,
             size_t const __num, int const __ghost)
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
  if (__ate->l_pages == __ate->n_pages)
    return 0;

  /* setup local variables */
  page_size = vmm.page_size;
  flags     = __ate->flags;
  end       = __beg+__num;

  if (VMM_GHOST == __ghost) {
    /* mmap temporary memory with write protection for loading from disk */
    addr = (uintptr_t)mmap(NULL, __num*page_size, PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_LOCKED, -1, 0);
    if ((uintptr_t)MAP_FAILED == addr)
      return -1;
  }
  else {
    addr = __ate->base+(__beg*page_size);
    ret  = mprotect((void*)addr, __num*page_size, PROT_WRITE);
    if (-1 == ret)
      return -1;
  }

  /* compute file name */
  if (0 > snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    (uintptr_t)__ate))
  {
    return -1;
  }
  /* open the file for reading */
  fd = libc_open(fname, O_RDONLY);
  if (-1 == fd)
    return -1;

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
        return -1;

      if (VMM_GHOST == __ghost) {
        /* give read permission to temporary pages */
        ret = mprotect((void*)(addr+((ipfirst-__beg)*page_size)),\
          (ip-ipfirst)*page_size, PROT_READ);
        if (-1 == ret)
          return -1;
        /* remap temporary pages into persistent memory */
        raddr = (uintptr_t)mremap((void*)(addr+((ipfirst-__beg)*page_size)),\
          (ip-ipfirst)*page_size, (ip-ipfirst)*page_size,\
          MREMAP_MAYMOVE|MREMAP_FIXED,
          (void*)(__ate->base+(ipfirst*page_size)));
        if (MAP_FAILED == (void*)raddr)
          return -1;
      }

      numrd += (ip-ipfirst);

      ipfirst = -1;
    }

    if (ip != end) {
      if (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) {
        ASSERT(__ate->l_pages < __ate->n_pages);
        __ate->l_pages++;

        /* flag: *0* */
        flags[ip] &= ~MMU_RSDNT;
      }
    }
  }

  /* close file */
  ret = close(fd);
  if (-1 == ret)
    return -1;

  if (VMM_GHOST == __ghost) {
    /* unmap any remaining temporary pages */
    ret = munmap((void*)addr, __num*page_size);
    if (-1 == ret)
      return -1;
  }
  else {
    /* update protection of temporary mapping to read-only */
    ret = mprotect((void*)addr, __num*page_size, PROT_READ);
    if (-1 == ret)
      return -1;

    /* update protection of temporary mapping and copy data for any dirty pages
     * */
    for (ip=__beg; ip<end; ++ip) {
      if (MMU_DIRTY == (flags[ip]&MMU_DIRTY)) {
        ret = mprotect((void*)(addr+((ip-__beg)*page_size)), page_size,\
          PROT_READ|PROT_WRITE);
        if (-1 == ret)
          return -1;
      }
    }
  }

  /* return the number of pages read from disk */
  return numrd;
}
SBMA_EXPORT(internal, ssize_t
__vmm_swap_i(struct ate * const __ate, size_t const __beg,
             size_t const __num, int const __ghost));


SBMA_EXTERN ssize_t
__vmm_swap_o(struct ate * const __ate, size_t const __beg,
             size_t const __num)
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
  if (0 == __ate->l_pages)
    return 0;

  /* setup local variables */
  page_size = vmm.page_size;
  addr      = __ate->base;
  flags     = __ate->flags;
  end       = __beg+__num;

  /* compute file name */
  if (0 > snprintf(fname, FILENAME_MAX, "%s%d-%zx", vmm.fstem, (int)getpid(),\
    (uintptr_t)__ate))
  {
    return -1;
  }
  /* open the file for writing */
  fd = libc_open(fname, O_WRONLY);
  if (-1 == fd)
    return -1;

  /* go over the pages and write the ones that have changed. perform the
   * writes in contigous chunks of changed pages. */
  for (ipfirst=-1,ip=__beg; ip<=end; ++ip) {
    if (ip != end && (MMU_DIRTY != (flags[ip]&MMU_DIRTY))) {
      if (MMU_RSDNT != (flags[ip]&MMU_RSDNT)) {
        ASSERT(__ate->l_pages > 0);
        __ate->l_pages--;
      }

      /* flag: 01* */
      flags[ip] &= MMU_ZFILL;
      flags[ip] |= MMU_RSDNT;
    }

    if (ip != end && (MMU_DIRTY == (flags[ip]&MMU_DIRTY))) {
      ASSERT(MMU_RSDNT != (flags[ip]&MMU_RSDNT)); /* is resident */

      if (-1 == ipfirst)
        ipfirst = ip;

      ASSERT(__ate->l_pages > 0);
      __ate->l_pages--;

      /* flag: 011 */
      flags[ip] = MMU_RSDNT|MMU_ZFILL;
    }
    else if (-1 != ipfirst) {
      ret = __vmm_write(fd, (void*)(addr+(ipfirst*page_size)),\
        (ip-ipfirst)*page_size, ipfirst*page_size);
      if (-1 == ret)
        return -1;

      numwr += (ip-ipfirst);

      ipfirst = -1;
    }
  }

  /* close file */
  ret = close(fd);
  if (-1 == ret)
    return -1;

  /* unlock the memory from RAM */
  ret = munlock((void*)(addr+(__beg*page_size)), __num*page_size);
  if (-1 == ret)
    return -1;

  /* update its protection to none */
  ret = mprotect((void*)(addr+(__beg*page_size)), __num*page_size, PROT_NONE);
  if (-1 == ret)
    return -1;

  /* unlock the memory, update its protection to none and advise kernel to
   * release its associated resources */
  ret = madvise((void*)(addr+(__beg*page_size)), __num*page_size,\
    MADV_DONTNEED);
  if (-1 == ret)
    return -1;

  /* return the number of pages written to disk */
  return numwr;
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
    }
    /* flag: 0*0 */
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

  /* set page size */
  __vmm->page_size = __page_size;

  /* set options */
  __vmm->opts = __opts;

  /* initialize statistics */
  __vmm->numipc   = 0;
  __vmm->numrf    = 0;
  __vmm->numwf    = 0;
  __vmm->numrd    = 0;
  __vmm->numwr    = 0;
  __vmm->curpages = 0;
  __vmm->maxpages = 0;
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
  if (-1 == __ipc_init(&(__vmm->ipc), __uniq, __n_procs, __max_mem))
    return -1;

  /* initialize vmm lock */
  if (-1 == __lock_init(&(__vmm->lock)))
    return -1;

  vmm.init = 1;

  ASSERT(0 == __ipc_is_eligible(&(__vmm->ipc)));

  return 0;
}
SBMA_EXPORT(internal, int
__vmm_init(struct vmm * const __vmm, char const * const __fstem,
           int const __uniq, size_t const __page_size, int const __n_procs,
           size_t const __max_mem, int const __opts));


SBMA_EXTERN int
__vmm_destroy(struct vmm * const __vmm)
{
  if (0 == vmm.init)
    return 0;

  vmm.init = 0;

  /* reset signal handler for SIGSEGV */
  if (-1 == sigaction(SIGSEGV, &(__vmm->oldact_segv), NULL))
    return -1;

  /* reset signal handler for SIGIPC */
  if (-1 == sigaction(SIGIPC, &(__vmm->oldact_ipc), NULL))
    return -1;

  /* destroy mmu */
  if (-1 == __mmu_destroy(&(__vmm->mmu)))
    return -1;

  /* destroy ipc */
  if (-1 == __ipc_destroy(&(__vmm->ipc)))
    return -1;

  /* destroy mmu lock */
  if (-1 == __lock_free(&(__vmm->lock)))
    return -1;

  return 0;
}
SBMA_EXPORT(internal, int
__vmm_destroy(struct vmm * const __vmm));
