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


#ifdef NDEBUG
# undef NDEBUG
#endif


#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t */
#include <sys/types.h> /* ssize_t */
#include "config.h"
#include "mmu.h"
#include "vmm.h"


/****************************************************************************/
/*! Count the number of pages to be loaded by a touch operation. */
/****************************************************************************/
static ssize_t
__ooc_mtouch_probe__(struct ate * const __ate, void * const __addr,
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

  return __vmm_to_sys__(l_pages);
}


/****************************************************************************/
/*! Internal: Touch the specified range. */
/****************************************************************************/
static ssize_t
__ooc_mtouch_int__(struct ate * const __ate, void * const __addr,
                   size_t const __len)
{
  size_t beg, end, page_size;
  ssize_t numrd;

  page_size = vmm.page_size;

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)__addr-__ate->base)/page_size;
  end = 1+(((uintptr_t)__addr+__len-__ate->base-1)/page_size);

  numrd = __vmm_swap_i__(__ate, beg, end-beg, vmm.opts&VMM_GHOST);
  if (-1 == numrd)
    return -1;

  return __vmm_to_sys__(numrd);
}


extern void
__ooc_check__(char const * const file, int const line)
{
  static int FAILED=0;
  int ret;
  size_t page_size, s_pages, n_pages, f_pages, l_pages;
  struct ate * ate;

  if (1 == FAILED)
    return;

  ret = LOCK_GET(&(vmm.lock));
  if (-1 == ret)
    printf("[%5d] %s:%d %s\n", (int)getpid(), basename(file), line,
      strerror(errno));

  page_size = vmm.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  l_pages = 0;
  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = LOCK_GET(&(ate->lock));
    if (-1 == ret)
      printf("[%5d] %s:%d %s\n", (int)getpid(), basename(file), line,
        strerror(errno));
    n_pages   = ate->n_pages;
    f_pages   = 1+((n_pages*sizeof(uint8_t)-1)/page_size);
    l_pages += (s_pages+ate->l_pages+f_pages);
    ret = LOCK_LET(&(ate->lock));
    if (-1 == ret)
      printf("[%5d] %s:%d %s\n", (int)getpid(), basename(file), line,
        strerror(errno));
  }
  l_pages = __vmm_to_sys__(l_pages);

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

  ret = LOCK_LET(&(vmm.lock));
  if (-1 == ret)
    printf("[%5d] %s:%d %s\n", (int)getpid(), basename(file), line,
      strerror(errno));

  /*if (NULL != file && 0 != line)
    printf("[%5d] %s:%d\n", (int)getpid(), basename(file), line);*/
  fflush(stdout);
}


/****************************************************************************/
/*! Touch the specified range. */
/****************************************************************************/
extern ssize_t
__ooc_mtouch__(void * const __addr, size_t const __len)
{
  int ret;
  ssize_t l_pages, numrd;
  struct ate * ate;

  //__ooc_check__(__FILE__, __LINE__);

  ate = __mmu_lookup_ate__(&(vmm.mmu), __addr);
  if (NULL == ate)
    return -1;

  /* check memory file to see if there is enough free memory to complete this
   * allocation. */
  for (;;) {
    l_pages = __ooc_mtouch_probe__(ate, __addr, __len);
    if (-1 == l_pages) {
      (void)LOCK_LET(&(ate->lock));
      return -1;
    }

    if (VMM_LZYWR != (vmm.opts&VMM_LZYWR)) {
      break;
    }
    else {
      assert(IPC_ELIGIBLE != (vmm.ipc.flags[vmm.ipc.id]&IPC_ELIGIBLE));

      ret = __ipc_madmit__(&(vmm.ipc), l_pages);
      if (-1 == ret) {
        if (EAGAIN == errno) {
          errno = 0;
        }
        else {
          (void)LOCK_LET(&(ate->lock));
          return -1;
        }
      }
      else {
        break;
      }
    }
  }

  numrd = __ooc_mtouch_int__(ate, __addr, __len);
  if (-1 == numrd) {
    (void)LOCK_LET(&(ate->lock));
    return -1;
  }

  ret = LOCK_LET(&(ate->lock));
  if (-1 == ret)
    return -1;

  /* track number of syspages currently loaded, number of syspages written to
   * disk, and high water mark for syspages loaded */
  __vmm_track__(curpages, l_pages);
  __vmm_track__(numrd, numrd);
  __vmm_track__(maxpages,\
    vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);

  //__ooc_check__(__FILE__, __LINE__);

  return l_pages;
}


/****************************************************************************/
/*! Touch all allocations. */
/****************************************************************************/
extern ssize_t
__ooc_mtouchall__(void)
{
  int ret;
  size_t l_pages=0, numrd=0;
  ssize_t retval;
  struct ate * ate;

  ret = LOCK_GET(&(vmm.lock));
  if (-1 == ret)
    return -1;

  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = LOCK_GET(&(ate->lock));
    if (-1 == ret) {
      (void)LOCK_LET(&(vmm.lock));
      return -1;
    }

    /* check memory file to see if there is enough free memory to complete
     * this allocation. */
    for (;;) {
      retval = __ooc_mtouch_probe__(ate, (void*)ate->base,\
        ate->n_pages*vmm.page_size);
      if (-1 == retval) {
        (void)LOCK_LET(&(ate->lock));
        (void)LOCK_LET(&(vmm.lock));
        return -1;
      }

      if (VMM_LZYWR != (vmm.opts&VMM_LZYWR)) {
        break;
      }
      else {
        assert(IPC_ELIGIBLE != (vmm.ipc.flags[vmm.ipc.id]&IPC_ELIGIBLE));

        ret = __ipc_madmit__(&(vmm.ipc), retval);
        if (-1 == ret) {
          if (EAGAIN == errno) {
            errno = 0;
          }
          else {
            (void)LOCK_LET(&(ate->lock));
            (void)LOCK_LET(&(vmm.lock));
            return -1;
          }
        }
        else {
          break;
        }
      }
    }

    ret = LOCK_LET(&(ate->lock));
    if (-1 == ret) {
      (void)LOCK_LET(&(vmm.lock));
      return -1;
    }

    l_pages += retval;
  }

  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = LOCK_GET(&(ate->lock));
    if (-1 == ret) {
      (void)LOCK_LET(&(vmm.lock));
      return -1;
    }
    ret = __ooc_mtouch_int__(ate, (void*)ate->base,\
      ate->n_pages*vmm.page_size);
    if (-1 == ret) {
      (void)LOCK_LET(&(ate->lock));
      (void)LOCK_LET(&(vmm.lock));
      return -1;
    }
    ret = LOCK_LET(&(ate->lock));
    if (-1 == ret) {
      (void)LOCK_LET(&(vmm.lock));
      return -1;
    }
    numrd += ret;
  }

  ret = LOCK_LET(&(vmm.lock));
  if (-1 == ret)
    return -1;

  /* track number of syspages currently loaded, number of syspages written to
   * disk, and high water mark for syspages loaded */
  __vmm_track__(curpages, l_pages);
  __vmm_track__(curpages, l_pages);
  __vmm_track__(numrd, numrd);
  __vmm_track__(maxpages,\
    vmm.curpages>vmm.maxpages?vmm.curpages-vmm.maxpages:0);

  //__ooc_check__(__FILE__, __LINE__);

  return l_pages;
}


/****************************************************************************/
/*! Clear the specified range. */
/****************************************************************************/
extern ssize_t
__ooc_mclear__(void * const __addr, size_t const __len)
{
  size_t beg, end, page_size;
  ssize_t ret;
  struct ate * ate;

  ate = __mmu_lookup_ate__(&(vmm.mmu), __addr);
  if (NULL == ate)
    return -1;

  page_size = vmm.page_size;

  /* can only clear pages fully within range, thus beg is a ceil
   * operation and end is a floor operation, except for when addr+len
   * consumes all of the last page, then end just equals n_pages. */
  beg = 1+(((uintptr_t)__addr-ate->base-1)/page_size);
  end = ((uintptr_t)__addr+__len-ate->base)/page_size;

  if (beg <= end) {
    ret = __vmm_swap_x__(ate, beg, end-beg);
    if (-1 == ret)
      return -1;
  }

  ret = LOCK_LET(&(ate->lock));
  if (-1 == ret)
    return -1;

  return 0;
}


/****************************************************************************/
/*! Clear all allocations. */
/****************************************************************************/
extern ssize_t
__ooc_mclearall__(void)
{
  ssize_t ret;
  struct ate * ate;

  ret = LOCK_GET(&(vmm.lock));
  if (-1 == ret)
    return -1;

  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = __ooc_mclear__((void*)ate->base, ate->n_pages*vmm.page_size);
    if (-1 == ret) {
      (void)LOCK_LET(&(vmm.lock));
      return -1;
    }
  }

  ret = LOCK_LET(&(vmm.lock));
  if (-1 == ret)
    return -1;

  return 0;
}


/****************************************************************************/
/*! Count the number of pages to be unloaded by a evict operation. */
/****************************************************************************/
static ssize_t
__ooc_mevict_probe__(struct ate * const __ate, void * const __addr,
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

  return __vmm_to_sys__(l_pages);
}


/****************************************************************************/
/*! Internal: Evict the allocation containing addr. */
/****************************************************************************/
static ssize_t
__ooc_mevict_int__(struct ate * const __ate, void * const __addr,
                   size_t const __len)
{
  size_t beg, end, page_size;
  ssize_t numwr;

  page_size = vmm.page_size;

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)__addr-__ate->base)/page_size;
  end = 1+(((uintptr_t)__addr+__len-__ate->base-1)/page_size);

  numwr = __vmm_swap_o__(__ate, beg, end-beg);
  if (-1 == numwr)
    return -1;

  return __vmm_to_sys__(numwr);
}


/****************************************************************************/
/*! Evict the allocation containing addr. */
/****************************************************************************/
extern ssize_t
__ooc_mevict__(void * const __addr, size_t const __len)
{
  int ret;
  ssize_t l_pages, numwr;
  struct ate * ate;

  ate = __mmu_lookup_ate__(&(vmm.mmu), __addr);
  if (NULL == ate)
    return -1;

  l_pages = __ooc_mevict_probe__(ate, __addr, __len);
  if (-1 == l_pages) {
    (void)LOCK_LET(&(ate->lock));
    return -1;
  }

  numwr = __ooc_mevict_int__(ate, __addr, __len);
  if (-1 == numwr) {
    (void)LOCK_LET(&(ate->lock));
    return -1;
  }

  /* update memory file */
  if (VMM_LZYWR == (vmm.opts&VMM_LZYWR)) {
    //printf("[%5d] %s:%d <%zu,%zu>\n", (int)getpid(), basename(__FILE__),
    //  __LINE__, vmm.curpages, vmm.ipc.pmem[vmm.ipc.id]);
    ret = __ipc_mevict__(&(vmm.ipc), -l_pages);
    if (-1 == ret)
      return -1;
  }

  /* track number of syspages currently loaded, number of syspages written to
   * disk, and high water mark for syspages loaded */
  if (l_pages > vmm.curpages)
    printf("[%5d] %s:%d\n", (int)getpid(), basename(__FILE__), __LINE__);
  __vmm_track__(curpages, -l_pages);
  __vmm_track__(numwr, numwr);

  //__ooc_check__(__FILE__, __LINE__);

  return l_pages;
}


/****************************************************************************/
/*! Internal: Evict all allocations. */
/****************************************************************************/
extern int
__ooc_mevictall_int__(size_t * const __l_pages, size_t * const __numwr)
{
  size_t l_pages=0, numwr=0;
  ssize_t ret;
  struct ate * ate;

  ret = LOCK_GET(&(vmm.lock));
  if (-1 == ret)
    return -1;

  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = LOCK_GET(&(ate->lock));
    if (-1 == ret) {
      (void)LOCK_LET(&(vmm.lock));
      return -1;
    }
    ret = __ooc_mevict_probe__(ate, (void*)ate->base,\
      ate->n_pages*vmm.page_size);
    if (-1 == ret) {
      (void)LOCK_LET(&(ate->lock));
      (void)LOCK_LET(&(vmm.lock));
      return -1;
    }
    l_pages += ret;
    ret = __ooc_mevict_int__(ate, (void*)ate->base,\
      ate->n_pages*vmm.page_size);
    if (-1 == ret) {
      (void)LOCK_LET(&(ate->lock));
      (void)LOCK_LET(&(vmm.lock));
      return -1;
    }
    numwr += ret;
    ret = LOCK_LET(&(ate->lock));
    if (-1 == ret) {
      (void)LOCK_LET(&(vmm.lock));
      return -1;
    }
  }

  ret = LOCK_LET(&(vmm.lock));
  if (-1 == ret)
    return -1;

  *__l_pages = l_pages;
  *__numwr   = numwr;

  return 0;
}


/****************************************************************************/
/*! Evict all allocations. */
/****************************************************************************/
extern ssize_t
__ooc_mevictall__(void)
{
  int ret;
  size_t l_pages, numwr;

  ret = __ooc_mevictall_int__(&l_pages, &numwr);
  if (-1 == ret)
    return -1;

  /* update memory file */
  if (VMM_LZYWR == (vmm.opts&VMM_LZYWR)) {
    //printf("[%5d] %s:%d <%zu,%zu>\n", (int)getpid(), basename(__FILE__),
    //  __LINE__, vmm.curpages, vmm.ipc.pmem[vmm.ipc.id]);
    ret = __ipc_mevict__(&(vmm.ipc), -l_pages);
    if (-1 == ret)
      return -1;
  }

  /* track number of syspages currently loaded, number of syspages written to
   * disk, and high water mark for syspages loaded */
  if (l_pages > vmm.curpages)
    printf("[%5d] %s:%d\n", (int)getpid(), basename(__FILE__), __LINE__);
  __vmm_track__(curpages, -l_pages);
  __vmm_track__(numwr, numwr);

  //__ooc_check__(__FILE__, __LINE__);

  return l_pages;
}


/****************************************************************************/
/*! Check if __addr exists in an allocation table entry. */
/****************************************************************************/
extern int
__ooc_mexist__(void const * const __addr)
{
  int ret;
  struct ate * ate;

  ate = __mmu_lookup_ate__(&(vmm.mmu), __addr);
  if (NULL == ate)
    return 0;

  ret = LOCK_LET(&(ate->lock));
  if (-1 == ret)
    return -1;

  return 1;
}
