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


#include <errno.h>     /* errno library */
#include <stdarg.h>    /* stdarg library */
#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t */
#include <sys/types.h> /* ssize_t */
#include <unistd.h>    /* syscall, _SC_PAGESIZE */
#include "config.h"
#include "ipc.h"
#include "lock.h"
#include "mmu.h"
#include "sbma.h"
#include "vmm.h"


#if 0
SBMA_EXTERN void
__sbma_check(char const * const file, int const line)
{
  static int FAILED=0;
  int ret;
  size_t page_size, s_pages, n_pages, f_pages, l_pages;
  struct ate * ate;

  if (1 == FAILED)
    return;

  ret = __lock_get(&(vmm.lock));
  if (-1 == ret)
    printf("[%5d] %s:%d %s\n", (int)getpid(), basename(file), line,
      strerror(errno));

  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  l_pages = 0;
  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = __lock_get(&(ate->lock));
    if (-1 == ret)
      printf("[%5d] %s:%d %s\n", (int)getpid(), basename(file), line,
        strerror(errno));
    n_pages   = ate->n_pages;
    f_pages   = 1+((n_pages*sizeof(uint8_t)-1)/page_size);
    l_pages += (s_pages+ate->l_pages+f_pages);
    ret = __lock_let(&(ate->lock));
    if (-1 == ret)
      printf("[%5d] %s:%d %s\n", (int)getpid(), basename(file), line,
        strerror(errno));
  }
  l_pages = VMM_TO_SYS(l_pages);

  if (l_pages != vmm.curpages) {
    printf("[%5d] %s:%d l_pages != vmm.curpages (%zu,%zu)\n", (int)getpid(),
      basename(file), line, l_pages, vmm.curpages);
    FAILED = 1;
  }
  if (l_pages != vmm.ipc.pmem[vmm.ipc.id]) {
    printf("[%5d] %s:%d l_pages != vmm.ipc.pmem[vmm.ipc.id] (%zu,%zu)\n",
      (int)getpid(), basename(file), line, l_pages,
      vmm.ipc.pmem[vmm.ipc.id]);
    FAILED = 1;
  }
  if (vmm.curpages != vmm.ipc.pmem[vmm.ipc.id]) {
    printf("[%5d] %s:%d vmm.curpages != vmm.ipc.pmem[vmm.ipc.id] (%zu,%zu)\n",
      (int)getpid(), basename(file), line, vmm.curpages,
      vmm.ipc.pmem[vmm.ipc.id]);
    FAILED = 1;
  }
  if (l_pages == vmm.curpages && l_pages == vmm.ipc.pmem[vmm.ipc.id])
    printf("[%5d] %s:%d success\n", (int)getpid(), basename(file), line);

  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    printf("[%5d] %s:%d %s\n", (int)getpid(), basename(file), line,
      strerror(errno));

  /*if (NULL != file && 0 != line)
    printf("[%5d] %s:%d\n", (int)getpid(), basename(file), line);*/
  fflush(stdout);
}
#endif


/****************************************************************************/
/*! Count the number of pages to be loaded by a touch operation. */
/****************************************************************************/
SBMA_STATIC ssize_t
__sbma_mtouch_probe(struct ate * const __ate, void * const __addr,
                    size_t const __len)
{
  size_t ip, beg, end, page_size, l_pages;
  uint8_t * flags;

  page_size = vmm.page_size;
  flags     = __ate->flags;

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)__addr-__ate->base)/page_size;
  end = 1+(((uintptr_t)__addr+__len-__ate->base-1)/page_size);

  for (l_pages=0,ip=beg; ip<end; ++ip) {
    if (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) /* not resident */
      l_pages++;
  }

  return VMM_TO_SYS(l_pages);
}


/****************************************************************************/
/*! Internal: Touch the specified range. */
/****************************************************************************/
SBMA_STATIC ssize_t
__sbma_mtouch_int(struct ate * const __ate, void * const __addr,
                  size_t const __len)
{
  size_t beg, end, page_size;
  ssize_t numrd;

  page_size = vmm.page_size;

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)__addr-__ate->base)/page_size;
  end = 1+(((uintptr_t)__addr+__len-__ate->base-1)/page_size);

  numrd = __vmm_swap_i(__ate, beg, end-beg, vmm.opts&VMM_GHOST);
  if (-1 == numrd)
    return -1;

  return VMM_TO_SYS(numrd);
}


/****************************************************************************/
/*! Count the number of pages to be unloaded by a evict operation. */
/****************************************************************************/
SBMA_STATIC ssize_t
__sbma_mevict_probe(struct ate * const __ate, void * const __addr,
                    size_t const __len)
{
  size_t ip, beg, end, page_size, l_pages;
  uint8_t * flags;

  page_size = vmm.page_size;
  flags     = __ate->flags;

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)__addr-__ate->base)/page_size;
  end = 1+(((uintptr_t)__addr+__len-__ate->base-1)/page_size);

  for (l_pages=0,ip=beg; ip<end; ++ip) {
    if (MMU_RSDNT != (flags[ip]&MMU_RSDNT)) /* resident */
      l_pages++;
  }

  return VMM_TO_SYS(l_pages);
}


/****************************************************************************/
/*! Internal: Evict the allocation containing addr. */
/****************************************************************************/
SBMA_STATIC ssize_t
__sbma_mevict_int(struct ate * const __ate, void * const __addr,
                  size_t const __len)
{
  size_t beg, end, page_size;
  ssize_t numwr;

  page_size = vmm.page_size;

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)__addr-__ate->base)/page_size;
  end = 1+(((uintptr_t)__addr+__len-__ate->base-1)/page_size);

  numwr = __vmm_swap_o(__ate, beg, end-beg);
  if (-1 == numwr)
    return -1;

  return VMM_TO_SYS(numwr);
}


/****************************************************************************/
/*! Touch the specified range. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mtouch(void * const __addr, size_t const __len)
{
  int ret;
  ssize_t l_pages, numrd;
  struct ate * ate;

  ate = __mmu_lookup_ate(&(vmm.mmu), __addr);
  if (NULL == ate)
    return -1;

  /* check memory file to see if there is enough free memory to complete this
   * allocation. */
  for (;;) {
    l_pages = __sbma_mtouch_probe(ate, __addr, __len);
    if (-1 == l_pages) {
      (void)__lock_let(&(ate->lock));
      return -1;
    }

#if SBMA_VERSION < 200
    if (VMM_LZYWR != (vmm.opts&VMM_LZYWR)) {
      break;
    }
#endif
    ASSERT(IPC_ELIGIBLE != (vmm.ipc.flags[vmm.ipc.id]&IPC_ELIGIBLE));

    ret = __ipc_madmit(&(vmm.ipc), l_pages);
    if (-1 == ret) {
      if (EAGAIN == errno) {
        errno = 0;
      }
      else {
        (void)__lock_let(&(ate->lock));
        return -1;
      }
    }
    else {
      break;
    }
  }

  numrd = __sbma_mtouch_int(ate, __addr, __len);
  if (-1 == numrd) {
    (void)__lock_let(&(ate->lock));
    return -1;
  }

  ret = __lock_let(&(ate->lock));
  if (-1 == ret)
    return -1;

  /* track number of syspages currently loaded, number of syspages written to
   * disk, and high water mark for syspages loaded */
  VMM_TRACK(curpages, l_pages);
  VMM_TRACK(numrd, numrd);
  VMM_TRACK(maxpages,\
    vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);

  return l_pages;
}
SBMA_EXPORT(internal, ssize_t
__sbma_mtouch(void * const __addr, size_t const __len));


/****************************************************************************/
/*! Touch the specified ranges. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mtouch_atomic(void * const __addr, size_t const __len, ...)
{
  int ret;
  ssize_t l_pages, numrd;
  struct ate * ate;

  ate = __mmu_lookup_ate(&(vmm.mmu), __addr);
  if (NULL == ate)
    return -1;

  /* check memory file to see if there is enough free memory to complete this
   * allocation. */
  for (;;) {
    l_pages = __sbma_mtouch_probe(ate, __addr, __len);
    if (-1 == l_pages) {
      (void)__lock_let(&(ate->lock));
      return -1;
    }

#if SBMA_VERSION < 200
    if (VMM_LZYWR != (vmm.opts&VMM_LZYWR)) {
      break;
    }
#endif
    ASSERT(IPC_ELIGIBLE != (vmm.ipc.flags[vmm.ipc.id]&IPC_ELIGIBLE));

    ret = __ipc_madmit(&(vmm.ipc), l_pages);
    if (-1 == ret) {
      if (EAGAIN == errno) {
        errno = 0;
      }
      else {
        (void)__lock_let(&(ate->lock));
        return -1;
      }
    }
    else {
      break;
    }
  }

  numrd = __sbma_mtouch_int(ate, __addr, __len);
  if (-1 == numrd) {
    (void)__lock_let(&(ate->lock));
    return -1;
  }

  ret = __lock_let(&(ate->lock));
  if (-1 == ret)
    return -1;

  /* track number of syspages currently loaded, number of syspages written to
   * disk, and high water mark for syspages loaded */
  VMM_TRACK(curpages, l_pages);
  VMM_TRACK(numrd, numrd);
  VMM_TRACK(maxpages,\
    vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);

  return l_pages;
}
SBMA_EXPORT(internal, ssize_t
__sbma_mtouch_atomic(void * const __addr, size_t const __len, ...));


/****************************************************************************/
/*! Touch all allocations. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mtouchall(void)
{
  int ret;
  size_t l_pages=0, numrd=0;
  ssize_t retval;
  struct ate * ate;

  ret = __lock_get(&(vmm.lock));
  if (-1 == ret)
    return -1;

  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = __lock_get(&(ate->lock));
    if (-1 == ret) {
      (void)__lock_let(&(vmm.lock));
      return -1;
    }

    /* check memory file to see if there is enough free memory to complete
     * this allocation. */
    for (;;) {
      retval = __sbma_mtouch_probe(ate, (void*)ate->base,\
        ate->n_pages*vmm.page_size);
      if (-1 == retval) {
        (void)__lock_let(&(ate->lock));
        (void)__lock_let(&(vmm.lock));
        return -1;
      }

#if SBMA_VERSION < 200
      if (VMM_LZYWR != (vmm.opts&VMM_LZYWR)) {
        break;
      }
#endif
      ASSERT(IPC_ELIGIBLE != (vmm.ipc.flags[vmm.ipc.id]&IPC_ELIGIBLE));

      ret = __ipc_madmit(&(vmm.ipc), retval);
      if (-1 == ret) {
        if (EAGAIN == errno) {
          errno = 0;
        }
        else {
          (void)__lock_let(&(ate->lock));
          (void)__lock_let(&(vmm.lock));
          return -1;
        }
      }
      else {
        break;
      }
    }

    ret = __lock_let(&(ate->lock));
    if (-1 == ret) {
      (void)__lock_let(&(vmm.lock));
      return -1;
    }

    l_pages += retval;
  }

  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = __lock_get(&(ate->lock));
    if (-1 == ret) {
      (void)__lock_let(&(vmm.lock));
      return -1;
    }
    ret = __sbma_mtouch_int(ate, (void*)ate->base,\
      ate->n_pages*vmm.page_size);
    if (-1 == ret) {
      (void)__lock_let(&(ate->lock));
      (void)__lock_let(&(vmm.lock));
      return -1;
    }
    ret = __lock_let(&(ate->lock));
    if (-1 == ret) {
      (void)__lock_let(&(vmm.lock));
      return -1;
    }
    numrd += ret;
  }

  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    return -1;

  /* track number of syspages currently loaded, number of syspages written to
   * disk, and high water mark for syspages loaded */
  VMM_TRACK(curpages, l_pages);
  VMM_TRACK(curpages, l_pages);
  VMM_TRACK(numrd, numrd);
  VMM_TRACK(maxpages,\
    vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);

  return l_pages;
}
SBMA_EXPORT(internal, ssize_t
__sbma_mtouchall(void));


/****************************************************************************/
/*! Clear the specified range. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mclear(void * const __addr, size_t const __len)
{
  size_t beg, end, page_size;
  ssize_t ret;
  struct ate * ate;

  ate = __mmu_lookup_ate(&(vmm.mmu), __addr);
  if (NULL == ate)
    return -1;

  page_size = vmm.page_size;

  /* can only clear pages fully within range, thus beg is a ceil
   * operation and end is a floor operation, except for when addr+len
   * consumes all of the last page, then end just equals n_pages. */
  beg = 1+(((uintptr_t)__addr-ate->base-1)/page_size);
  end = ((uintptr_t)__addr+__len-ate->base)/page_size;

  if (beg <= end) {
    ret = __vmm_swap_x(ate, beg, end-beg);
    if (-1 == ret)
      return -1;
  }

  ret = __lock_let(&(ate->lock));
  if (-1 == ret)
    return -1;

  return 0;
}
SBMA_EXPORT(internal, ssize_t
__sbma_mclear(void * const __addr, size_t const __len));


/****************************************************************************/
/*! Clear all allocations. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mclearall(void)
{
  ssize_t ret;
  struct ate * ate;

  ret = __lock_get(&(vmm.lock));
  if (-1 == ret)
    return -1;

  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = __sbma_mclear((void*)ate->base, ate->n_pages*vmm.page_size);
    if (-1 == ret) {
      (void)__lock_let(&(vmm.lock));
      return -1;
    }
  }

  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    return -1;

  return 0;
}
SBMA_EXPORT(internal, ssize_t
__sbma_mclearall(void));


/****************************************************************************/
/*! Evict the allocation containing addr. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mevict(void * const __addr, size_t const __len)
{
  int ret;
  ssize_t l_pages, numwr;
  struct ate * ate;

  ate = __mmu_lookup_ate(&(vmm.mmu), __addr);
  if (NULL == ate)
    return -1;

  l_pages = __sbma_mevict_probe(ate, __addr, __len);
  if (-1 == l_pages) {
    (void)__lock_let(&(ate->lock));
    return -1;
  }

  numwr = __sbma_mevict_int(ate, __addr, __len);
  if (-1 == numwr) {
    (void)__lock_let(&(ate->lock));
    return -1;
  }

  /* update memory file */
#if SBMA_VERSION < 200
  if (VMM_LZYWR == (vmm.opts&VMM_LZYWR)) {
#endif
    ret = __ipc_mevict(&(vmm.ipc), -l_pages);
    if (-1 == ret)
      return -1;
#if SBMA_VERSION < 200
  }
#endif

  /* track number of syspages currently loaded, number of syspages written to
   * disk, and high water mark for syspages loaded */
  VMM_TRACK(curpages, -l_pages);
  VMM_TRACK(numwr, numwr);

  return l_pages;
}
SBMA_EXPORT(internal, ssize_t
__sbma_mevict(void * const __addr, size_t const __len));


/****************************************************************************/
/*! Internal: Evict all allocations. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_mevictall_int(size_t * const __l_pages, size_t * const __numwr)
{
  size_t l_pages=0, numwr=0;
  ssize_t ret;
  struct ate * ate;

  ret = __lock_get(&(vmm.lock));
  if (-1 == ret)
    return -1;

  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = __lock_get(&(ate->lock));
    if (-1 == ret) {
      (void)__lock_let(&(vmm.lock));
      return -1;
    }
    ret = __sbma_mevict_probe(ate, (void*)ate->base,\
      ate->n_pages*vmm.page_size);
    if (-1 == ret) {
      (void)__lock_let(&(ate->lock));
      (void)__lock_let(&(vmm.lock));
      return -1;
    }
    l_pages += ret;
    ret = __sbma_mevict_int(ate, (void*)ate->base,\
      ate->n_pages*vmm.page_size);
    if (-1 == ret) {
      (void)__lock_let(&(ate->lock));
      (void)__lock_let(&(vmm.lock));
      return -1;
    }
    numwr += ret;
    ret = __lock_let(&(ate->lock));
    if (-1 == ret) {
      (void)__lock_let(&(vmm.lock));
      return -1;
    }
  }

  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    return -1;

  /* mark the process as unpopulated */
  vmm.ipc.flags[vmm.ipc.id] &= ~IPC_POPULATED;

  *__l_pages = l_pages;
  *__numwr   = numwr;

  return 0;
}
SBMA_EXPORT(internal, int
__sbma_mevictall_int(size_t * const __l_pages, size_t * const __numwr));


/****************************************************************************/
/*! Evict all allocations. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mevictall(void)
{
  int ret;
  size_t l_pages, numwr;

  ret = __sbma_mevictall_int(&l_pages, &numwr);
  if (-1 == ret)
    return -1;

  /* update memory file */
#if SBMA_VERSION < 200
  if (VMM_LZYWR == (vmm.opts&VMM_LZYWR)) {
#endif
    ret = __ipc_mevict(&(vmm.ipc), -l_pages);
    if (-1 == ret)
      return -1;
#if SBMA_VERSION < 200
  }
#endif

  /* track number of syspages currently loaded, number of syspages written to
   * disk, and high water mark for syspages loaded */
  VMM_TRACK(curpages, -l_pages);
  VMM_TRACK(numwr, numwr);

  return l_pages;
}
SBMA_EXPORT(internal, ssize_t
__sbma_mevictall(void));


/****************************************************************************/
/*! Check if __addr exists in an allocation table entry. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_mexist(void const * const __addr)
{
  int ret;
  struct ate * ate;

  ate = __mmu_lookup_ate(&(vmm.mmu), __addr);
  if (NULL == ate)
    return 0;

  ret = __lock_let(&(ate->lock));
  if (-1 == ret)
    return -1;

  return 1;
}
SBMA_EXPORT(internal, int
__sbma_mexist(void const * const __addr));
