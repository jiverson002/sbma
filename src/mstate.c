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
#include <time.h>      /* struct timespec */
#include <unistd.h>    /* syscall, _SC_PAGESIZE */
#include "common.h"
#include "ipc.h"
#include "lock.h"
#include "mmu.h"
#include "sbma.h"
#include "vmm.h"


/****************************************************************************/
/*! Count the number of pages to be charged by a touch operation. */
/****************************************************************************/
SBMA_STATIC ssize_t
__sbma_mtouch_probe(struct ate * const __ate, void * const __addr,
                    size_t const __len)
{
  size_t ip, beg, end, page_size, c_pages;
  volatile uint8_t * flags;

  if (((VMM_AGGCH|VMM_LZYRD) == (vmm.opts&(VMM_AGGCH|VMM_LZYRD))) &&\
      (0 == __ate->c_pages))
  {
    return VMM_TO_SYS(__ate->n_pages);
  }

  page_size = vmm.page_size;
  flags     = __ate->flags;

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)__addr-__ate->base)/page_size;
  end = 1+(((uintptr_t)__addr+__len-__ate->base-1)/page_size);

  for (c_pages=0,ip=beg; ip<end; ++ip) {
    if (MMU_CHRGD == (flags[ip]&MMU_CHRGD)) { /* not charged */
      ASSERT(MMU_RSDNT == (flags[ip]&MMU_RSDNT)); /* not resident */
      c_pages++;
    }
  }

  return VMM_TO_SYS(c_pages);
}


/****************************************************************************/
/*! Internal: Touch the specified range. */
/****************************************************************************/
SBMA_STATIC ssize_t
__sbma_mtouch_int(struct ate * const __ate, void * const __addr,
                  size_t const __len)
{
  size_t i, beg, end, page_size;
  ssize_t numrd;

  if (((VMM_AGGCH|VMM_LZYRD) == (vmm.opts&(VMM_AGGCH|VMM_LZYRD))) &&\
      (0 == __ate->c_pages))
  {
    for (i=0; i<__ate->n_pages; ++i) {
      /* flag: 0*** */
      __ate->flags[i] &= ~MMU_CHRGD;
    }
    __ate->c_pages = __ate->n_pages;
  }

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
/*! Count the number of pages to be discharged by a evict operation. */
/****************************************************************************/
SBMA_STATIC ssize_t
__sbma_mevict_probe(struct ate * const __ate, void * const __addr,
                    size_t const __len, size_t * const __c_pages,
                    size_t * const __d_pages)
{
  size_t ip, beg, end, page_size, c_pages, d_pages;
  volatile uint8_t * flags;

  page_size = vmm.page_size;
  flags     = __ate->flags;

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)__addr-__ate->base)/page_size;
  end = 1+(((uintptr_t)__addr+__len-__ate->base-1)/page_size);

  for (c_pages=0,d_pages=0,ip=beg; ip<end; ++ip) {
    if (MMU_CHRGD != (flags[ip]&MMU_CHRGD)) /* is charged */
      c_pages++;
    if (MMU_DIRTY == (flags[ip]&MMU_DIRTY)) /* is dirty */
      d_pages++;
  }

  *__c_pages = VMM_TO_SYS(c_pages);
  *__d_pages = VMM_TO_SYS(d_pages);

  return 0;
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
/*! Count the number of dirty pages to be cleared by a clear operation. */
/****************************************************************************/
SBMA_STATIC ssize_t
__sbma_mclear_probe(struct ate * const __ate, void * const __addr,
                    size_t const __len, size_t * const __d_pages)
{
  size_t ip, beg, end, page_size, d_pages;
  volatile uint8_t * flags;

  page_size = vmm.page_size;
  flags     = __ate->flags;

  /* can only clear pages fully within range, thus beg is a ceil
   * operation and end is a floor operation, except for when addr+len
   * consumes all of the last page, then end just equals n_pages. */
  if ((uintptr_t)__addr == __ate->base)
    beg = 0;
  else
    beg = 1+(((uintptr_t)__addr-__ate->base-1)/page_size);
  end = ((uintptr_t)__addr+__len-__ate->base)/page_size;

  for (d_pages=0,ip=beg; ip<end; ++ip) {
    if (MMU_DIRTY == (flags[ip]&MMU_DIRTY)) /* is dirty */
      d_pages++;
  }

  *__d_pages = VMM_TO_SYS(d_pages);

  return 0;
}


/****************************************************************************/
/*! Internal: Clear the allocation containing addr. */
/****************************************************************************/
SBMA_STATIC ssize_t
__sbma_mclear_int(struct ate * const __ate, void * const __addr,
                  size_t const __len)
{
  size_t beg, end, page_size;
  ssize_t ret;

  page_size = vmm.page_size;

  /* can only clear pages fully within range, thus beg is a ceil
   * operation and end is a floor operation, except for when addr+len
   * consumes all of the last page, then end just equals n_pages. */
  if ((uintptr_t)__addr == __ate->base)
    beg = 0;
  else
    beg = 1+(((uintptr_t)__addr-__ate->base-1)/page_size);
  end = ((uintptr_t)__addr+__len-__ate->base)/page_size;

  if (beg <= end) {
    ret = __vmm_swap_x(__ate, beg, end-beg);
    if (-1 == ret)
      return -1;
  }

  return 0;
}


/****************************************************************************/
/*! Check to make sure that the state of the vmm is consistent. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_check(char const * const __func, int const __line)
{
  int ret, retval=0;
  size_t i, c, l, d;
  size_t c_pages=0, d_pages=0, s_pages, f_pages;
  struct ate * ate;

  if (VMM_CHECK == (vmm.opts&VMM_CHECK)) {
    ret = __lock_get(&(vmm.lock));
    if (-1 == ret)
      goto CLEANUP1;

    for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
      ret = __lock_get(&(ate->lock));
      if (-1 == ret)
        goto CLEANUP2;

      if (VMM_METACH == (vmm.opts&VMM_METACH)) {
        s_pages  = 1+((sizeof(struct ate)-1)/vmm.page_size);
        f_pages  = 1+((ate->n_pages*sizeof(uint8_t)-1)/vmm.page_size);
      }
      else {
        s_pages  = 0;
        f_pages  = 0;
      }
      c_pages += s_pages+ate->c_pages+f_pages;
      d_pages += ate->d_pages;

      if (VMM_EXTRA == (vmm.opts&VMM_EXTRA)) {
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

      ret = __lock_let(&(ate->lock));
      if (-1 == ret)
        goto CLEANUP2;
    }

    if (VMM_TO_SYS(c_pages) != vmm.ipc.c_mem[vmm.ipc.id]) {
      printf("[%5d] %s:%d c_pages (%zu) != c_mem[id] (%zu)\n", (int)getpid(),
        __func, __line, VMM_TO_SYS(c_pages), vmm.ipc.c_mem[vmm.ipc.id]);
      retval = -1;
    }
    if (VMM_TO_SYS(d_pages) != vmm.ipc.d_mem[vmm.ipc.id]) {
      printf("[%5d] %s:%d d_pages (%zu) != d_mem[id] (%zu)\n", (int)getpid(),
        __func, __line, VMM_TO_SYS(d_pages), vmm.ipc.d_mem[vmm.ipc.id]);
      retval = -1;
    }

    ret = __lock_let(&(vmm.lock));
    if (-1 == ret)
      goto CLEANUP1;
  }
  else {
    retval = 0;
  }

  return retval;

  CLEANUP2:
  ret = __lock_let(&(ate->lock));
  ASSERT(-1 != ret);
  CLEANUP1:
  ret = __lock_let(&(vmm.lock));
  ASSERT(-1 != ret);
  return -1;
}
SBMA_EXPORT(internal, int
__sbma_check(char const * const __func, int const __line));


/****************************************************************************/
/*! Touch the specified range. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mtouch(void * const __ate, void * const __addr, size_t const __len)
{
  int ret;
  ssize_t c_pages, numrd=0;
  struct timespec tmr;
  struct ate * ate;

  /*========================================================================*/
  SBMA_STATE_CHECK();
  TIMER_START(&(tmr));
  /*========================================================================*/

  if (NULL == __ate) {
    ate = __mmu_lookup_ate(&(vmm.mmu), __addr);
    if ((struct ate*)-1 == ate || NULL == ate)
      goto ERREXIT;
  }
  else {
    ate = (struct ate *)__ate;
  }

  /* check memory file to see if there is enough free memory to complete this
   * allocation. */
  for (;;) {
    c_pages = __sbma_mtouch_probe(ate, __addr, __len);
    if (-1 == c_pages)
      goto CLEANUP;

    if (0 == c_pages)
      break;

    ret = __ipc_madmit(&(vmm.ipc), c_pages, vmm.opts&VMM_ADMITD);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }

  numrd = __sbma_mtouch_int(ate, __addr, __len);
  if (-1 == numrd)
    goto CLEANUP;

  if (NULL == __ate) {
    ret = __lock_let(&(ate->lock));
    if (-1 == ret)
      goto CLEANUP;
  }

  /*========================================================================*/
  TIMER_STOP(&(tmr));
  SBMA_STATE_CHECK();
  /*========================================================================*/

  VMM_TRACK(numrd, numrd);
  VMM_TRACK(tmrrd, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);

  return c_pages;

  CLEANUP:
  if (NULL == __ate) {
    ret = __lock_let(&(ate->lock));
    ASSERT(-1 != ret);
  }
  ERREXIT:
  return -1;
}
SBMA_EXPORT(internal, ssize_t
__sbma_mtouch(void * const __ate, void * const __addr, size_t const __len));


/****************************************************************************/
/*! Touch the specified ranges. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mtouch_atomic(void * const __addr, size_t const __len, ...)
{
  int ret;
  size_t i, num, _len, c_pages;
  size_t mnlen_, mxlen_, mxbeg_, mnend_;
  ssize_t _c_pages, _numrd, numrd;
  uintptr_t min_, max_;
  va_list args;
  struct timespec tmr;
  void * _addr;
  int dup[SBMA_ATOMIC_MAX];
  size_t len[SBMA_ATOMIC_MAX];
  void * addr[SBMA_ATOMIC_MAX];
  struct ate * _ate, * mnate_, * mxate_, * ate[SBMA_ATOMIC_MAX];

  if (NULL == __addr)
    return 0;

  /*========================================================================*/
  SBMA_STATE_CHECK();
  TIMER_START(&(tmr));
  /*========================================================================*/

  /* populate the arrays with the variable number of pointers and lengths */
  num   = 0;
  _addr = __addr;
  _len  = __len;
  va_start(args, __len);
  while (SBMA_ATOMIC_END != _addr) {
    _ate = __mmu_lookup_ate(&(vmm.mmu), _addr);
    if (NULL == _ate)
      goto NEXT;
    else if ((struct ate*)-1 == _ate)
      goto CLEANUP;

    for (i=0; i<num; ++i) {
      /* these could have pages overlapping even if the actual addresses don't
       * overlap */
      if (_ate == ate[i]) {
        min_   = (uintptr_t)(_addr < addr[i] ? _addr : addr[i]);
        max_   = (uintptr_t)((uintptr_t)_addr != min_ ? _addr : addr[i]);
        mnlen_ = (uintptr_t)_addr == min_ ? _len : len[i];
        mxlen_ = (uintptr_t)_addr == max_ ? _len : len[i];
        mnate_ = (uintptr_t)_addr == min_ ? _ate : ate[i];
        mxate_ = (uintptr_t)_addr == max_ ? _ate : ate[i];
        mnend_ = 1+((min_+mnlen_-mnate_->base-1)/vmm.page_size);
        mxbeg_ = (max_-mxate_->base)/vmm.page_size;

        /* overlapping page ranges of _ate */
        if (mnend_ >= mxbeg_) {
          /* [max_..max_+mxlen_) is completely overlapped by [min_..min_+minlen_) */
          if (min_+mnlen_ >= max_+mxlen_) {
            addr[i] = (void*)min_;
            len[i]  = mnlen_;
          }
          /* [max_..max_+mxlen_) is partially overlapped by [min_..min_+minlen_) */
          else {
            addr[i] = (void*)min_;
            len[i]  = max_+mxlen_-min_;
          }
          /* release the most recent recursive lock on _ate */
          ret = __lock_let(&(_ate->lock));
          if (-1 == ret)
            goto CLEANUP;
        }
        /* two distinct ranges of _ate */
        else {
          dup[num]   = 1;
          addr[num]  = _addr;
          len[num]   = _len;
          ate[num++] = _ate;
        }
        break;
      }
    }
    if (i == num) {
      dup[num]   = 0;
      addr[num]  = _addr;
      len[num]   = _len;
      ate[num++] = _ate;
    }

    NEXT:
    _addr = va_arg(args, void *);
    if (SBMA_ATOMIC_END != _addr)
      _len = va_arg(args, size_t);
  }
  va_end(args);

  /* check memory file to see if there is enough free memory to admit the
   * required amount of memory. */
  for (;;) {
    for (c_pages=0,i=0; i<num; ++i) {
      /* This is to avoid double counting under the following circumstances.
       * If aggressive charging is enabled (only applicable to lazy reading),
       * then if the number of pages charged to the current ate is zero
       * (0 == ate[i]->c_pages), mprobe will return the number of pages in the
       * ate (ate->n_pages) due to aggressive charging. This will lead to
       * counting said number of pages multiple times if multiple ranges from
       * the particular ate are being touched. Thus, we will only charge if
       * one of the following conditions is true (in order):
       *  1) aggressive charging is disabled
       *  2) the range to be loaded is the only range from an ate
       *  3) mprobe actually computes the number of pages to be charged,
       *     doesn't just shortcut and return ate->n_pages. It is sufficient
       *     to check if 0 != ate[i]->c_pages to satisfy this. */
      if (((VMM_AGGCH|VMM_LZYRD) != (vmm.opts&(VMM_AGGCH|VMM_LZYRD))) ||\
          (0 == dup[i]) || (0 != ate[i]->c_pages))
      {
        _c_pages = __sbma_mtouch_probe(ate[i], addr[i], len[i]);
        if (-1 == _c_pages)
          goto CLEANUP;

        c_pages += _c_pages;
      }
      else {
        ASSERT(0);
      }
    }

    if (0 == c_pages)
      break;

    ret = __ipc_madmit(&(vmm.ipc), c_pages, vmm.opts&VMM_ADMITD);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }

  /* touch each of the pointers */
  for (numrd=0,i=0; i<num; ++i) {
    _numrd = __sbma_mtouch_int(ate[i], addr[i], len[i]);
    if (-1 == _numrd)
      goto CLEANUP;
    numrd += _numrd;

    ret = __lock_let(&(ate[i]->lock));
    if (-1 == ret)
      goto CLEANUP;

    ate[i] = NULL; /* clear in case of failure */
  }

  /*========================================================================*/
  TIMER_STOP(&(tmr));
  SBMA_STATE_CHECK();
  /*========================================================================*/

  VMM_TRACK(numrd, numrd);
  VMM_TRACK(tmrrd, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);

  return c_pages;

  CLEANUP:
  for (i=0; i<num; ++i) {
    if (NULL != ate[i]) {
      ret = __lock_let(&(ate[i]->lock));
      ASSERT(-1 != ret);
    }
  }
  return -1;
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
  size_t c_pages, numrd;
  ssize_t retval;
  struct timespec tmr;
  struct ate * ate, * start=NULL, * stop=NULL;

  /*========================================================================*/
  SBMA_STATE_CHECK();
  TIMER_START(&(tmr));
  /*========================================================================*/

  ret = __lock_get(&(vmm.lock));
  if (-1 == ret)
    goto ERREXIT;

  /* Lock all allocations */
  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = __lock_get(&(ate->lock));
    if (-1 == ret) {
      stop = ate;
      goto CLEANUP;
    }

    if (NULL == start)
      start = ate;
  }

  /* check memory file to see if there is enough free memory to complete
   * this allocation. */
  for (;;) {
    for (c_pages=0,ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
      retval = __sbma_mtouch_probe(ate, (void*)ate->base,\
        ate->n_pages*vmm.page_size);
      if (-1 == retval)
        goto CLEANUP;
      c_pages += retval;
    }

    if (0 == c_pages)
      break;

    ret = __ipc_madmit(&(vmm.ipc), c_pages, vmm.opts&VMM_ADMITD);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }

  /* touch the memory */
  for (numrd=0,ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    retval = __sbma_mtouch_int(ate, (void*)ate->base,\
      ate->n_pages*vmm.page_size);
    if (-1 == retval)
      goto CLEANUP;
    ASSERT(ate->l_pages == ate->n_pages);
    ASSERT(ate->c_pages == ate->n_pages);

    ret = __lock_let(&(ate->lock));
    if (-1 == ret)
      goto CLEANUP;
    numrd += retval;

    start = ate->next;
  }

  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    goto CLEANUP;

  /*========================================================================*/
  TIMER_STOP(&(tmr));
  SBMA_STATE_CHECK();
  /*========================================================================*/

  VMM_TRACK(numrd, numrd);
  VMM_TRACK(tmrrd, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);

  return c_pages;

  CLEANUP:
  for (ate=start; stop!=ate; ate=ate->next) {
    ret = __lock_let(&(ate->lock));
    ASSERT(-1 != ret);
  }
  ret = __lock_let(&(vmm.lock));
  ASSERT(-1 != ret);
  ERREXIT:
  return -1;
}
SBMA_EXPORT(internal, ssize_t
__sbma_mtouchall(void));


/****************************************************************************/
/*! Clear the specified range. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mclear(void * const __addr, size_t const __len)
{
  size_t beg, end, page_size, c_pages, d_pages;
  ssize_t ret;
  struct ate * ate;

  SBMA_STATE_CHECK();

  ate = __mmu_lookup_ate(&(vmm.mmu), __addr);
  if ((struct ate*)-1 == ate || NULL == ate)
    goto ERREXIT;

  ret = __sbma_mclear_probe(ate, __addr, __len, &d_pages);
  if (-1 == ret)
    goto CLEANUP;

  ret = __sbma_mclear_int(ate, __addr, __len);
  if (-1 == ret)
    goto CLEANUP;

  /* update memory file */
  /* TODO can this be outside of __lock_let? */
  for (;;) {
    ret = __ipc_mevict(&(vmm.ipc), 0, d_pages);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }

  ret = __lock_let(&(ate->lock));
  if (-1 == ret)
    goto CLEANUP;

  SBMA_STATE_CHECK();
  return 0;

  CLEANUP:
  ret = __lock_let(&(ate->lock));
  ASSERT(-1 != ret);
  ERREXIT:
  return -1;
}
SBMA_EXPORT(internal, ssize_t
__sbma_mclear(void * const __addr, size_t const __len));


/****************************************************************************/
/*! Clear all allocations. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mclearall(void)
{
  size_t d_pages=0;
  ssize_t ret;
  struct ate * ate;

  SBMA_STATE_CHECK();

  ret = __lock_get(&(vmm.lock));
  if (-1 == ret)
    goto ERREXIT;

  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    d_pages += ate->d_pages;

    ret = __sbma_mclear((void*)ate->base, ate->n_pages*vmm.page_size);
    if (-1 == ret)
      goto CLEANUP;

    ASSERT(0 == ate->d_pages);
  }

  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    goto CLEANUP;

  ret = __ipc_mdirty(&(vmm.ipc), -VMM_TO_SYS(d_pages));
  if (-1 == ret)
    goto CLEANUP;

  SBMA_STATE_CHECK();
  return 0;

  CLEANUP:
  ret = __lock_let(&(vmm.lock));
  ASSERT(-1 != ret);
  ERREXIT:
  return -1;
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
  size_t c_pages, d_pages;
  ssize_t numwr;
  struct timespec tmr;
  struct ate * ate;

  /*========================================================================*/
  SBMA_STATE_CHECK();
  TIMER_START(&(tmr));
  /*========================================================================*/

  ate = __mmu_lookup_ate(&(vmm.mmu), __addr);
  if ((struct ate*)-1 == ate || NULL == ate)
    goto ERREXIT;

  ret = __sbma_mevict_probe(ate, __addr, __len, &c_pages, &d_pages);
  if (-1 == ret)
    goto CLEANUP;

  numwr = __sbma_mevict_int(ate, __addr, __len);
  if (-1 == numwr)
    goto CLEANUP;

  /* update memory file */
  /* TODO can this be outside of __lock_let? */
  for (;;) {
    ret = __ipc_mevict(&(vmm.ipc), c_pages, d_pages);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }

  ret = __lock_let(&(ate->lock));
  if (-1 == ret)
    goto CLEANUP;

  /*========================================================================*/
  TIMER_STOP(&(tmr));
  SBMA_STATE_CHECK();
  /*========================================================================*/

  VMM_TRACK(numwr, numwr);
  VMM_TRACK(tmrwr, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);

  return c_pages;

  CLEANUP:
  ret = __lock_let(&(ate->lock));
  ASSERT(-1 != ret);
  ERREXIT:
  return -1;
}
SBMA_EXPORT(internal, ssize_t
__sbma_mevict(void * const __addr, size_t const __len));


/****************************************************************************/
/*! Internal: Evict all allocations. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_mevictall_int(size_t * const __c_pages, size_t * const __d_pages,
                     size_t * const __numwr)
{
  size_t c_pages=0, d_pages=0, numwr=0;
  ssize_t ret;
  struct ate * ate;

  ret = __lock_get(&(vmm.lock));
  if (-1 == ret)
    goto ERREXIT;

  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = __lock_get(&(ate->lock));
    if (-1 == ret)
      goto CLEANUP1;
    c_pages += ate->c_pages;
    d_pages += ate->d_pages;
    ret = __sbma_mevict_int(ate, (void*)ate->base,\
      ate->n_pages*vmm.page_size);
    if (-1 == ret)
      goto CLEANUP2;
    numwr += ret;
    ASSERT(0 == ate->l_pages);
    ASSERT(0 == ate->c_pages);
    ASSERT(0 == ate->d_pages);
    ret = __lock_let(&(ate->lock));
    if (-1 == ret)
      goto CLEANUP2;
  }

  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    goto CLEANUP1;

  *__c_pages = VMM_TO_SYS(c_pages);
  *__d_pages = VMM_TO_SYS(d_pages);
  *__numwr   = VMM_TO_SYS(numwr);

  return 0;

  CLEANUP2:
  ret = __lock_let(&(ate->lock));
  ASSERT(-1 != ret);
  CLEANUP1:
  ret = __lock_let(&(vmm.lock));
  ASSERT(-1 != ret);
  ERREXIT:
  return -1;
}
SBMA_EXPORT(internal, int
__sbma_mevictall_int(size_t * const __c_pages, size_t * const __d_pages,
                     size_t * const __numwr));


/****************************************************************************/
/*! Evict all allocations. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mevictall(void)
{
  int ret;
  size_t c_pages, d_pages, numwr;
  struct timespec tmr;

  /*========================================================================*/
  SBMA_STATE_CHECK();
  TIMER_START(&(tmr));
  /*========================================================================*/

  ret = __sbma_mevictall_int(&c_pages, &d_pages, &numwr);
  if (-1 == ret)
    goto ERREXIT;

  /* update memory file */
  for (;;) {
    ret = __ipc_mevict(&(vmm.ipc), c_pages, d_pages);
    if (-1 == ret)
      goto ERREXIT;
    else if (-2 != ret)
      break;
  }

  /* change my status to unpopulated - must be before any potential waiting,
   * since SIGIPC could be raised again then. */
  __ipc_unpopulate(&(vmm.ipc));

  /*========================================================================*/
  TIMER_STOP(&(tmr));
  SBMA_STATE_CHECK();
  /*========================================================================*/

  VMM_TRACK(numwr, numwr);
  VMM_TRACK(tmrwr, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);

  return c_pages;

  ERREXIT:
  return -1;
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

  if (0 == vmm.init)
    return 0;

  ate = __mmu_lookup_ate(&(vmm.mmu), __addr);
  if ((struct ate*)-1 == ate)
    return -1;
  else if (NULL == ate)
    return 0;

  ret = __lock_let(&(ate->lock));
  if (-1 == ret)
    goto CLEANUP;

  return 1;

  CLEANUP:
  ret = __lock_let(&(ate->lock));
  ASSERT(-1 != ret);
  return -1;
}
SBMA_EXPORT(internal, int
__sbma_mexist(void const * const __addr));
