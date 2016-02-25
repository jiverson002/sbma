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


#include <stdarg.h>    /* stdarg library */
#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t */
#include <sys/types.h> /* ssize_t */
#include <time.h>      /* struct timespec */
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
sbma_mtouch_probe(struct ate * const __ate, void * const __addr,
                  size_t const __len)
{
  size_t ip, beg, end, page_size, c_pages;
  volatile uint8_t * flags;

  if (((VMM_AGGCH|VMM_LZYRD) == (_vmm_.opts&(VMM_AGGCH|VMM_LZYRD))) &&\
      (0 == __ate->c_pages))
  {
    return VMM_TO_SYS(__ate->n_pages);
  }

  page_size = _vmm_.page_size;
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
sbma_mtouch_int(struct ate * const __ate, void * const __addr,
                size_t const __len)
{
  size_t i, beg, end, page_size;
  ssize_t numrd;

  if (((VMM_AGGCH|VMM_LZYRD) == (_vmm_.opts&(VMM_AGGCH|VMM_LZYRD))) &&\
      (0 == __ate->c_pages))
  {
    for (i=0; i<__ate->n_pages; ++i) {
      /* flag: 0*** */
      __ate->flags[i] &= ~MMU_CHRGD;
    }
    __ate->c_pages = __ate->n_pages;
  }

  page_size = _vmm_.page_size;

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)__addr-__ate->base)/page_size;
  end = 1+(((uintptr_t)__addr+__len-__ate->base-1)/page_size);

  numrd = vmm_swap_i(__ate, beg, end-beg, _vmm_.opts&VMM_GHOST);
  if (-1 == numrd)
    return -1;
  return VMM_TO_SYS(numrd);
}


/****************************************************************************/
/*! Touch the specified range. */
/****************************************************************************/
SBMA_EXTERN ssize_t
sbma_mtouch(void * const __ate, void * const __addr, size_t const __len)
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
    ate = mmu_lookup_ate(&(_vmm_.mmu), __addr);
    if ((struct ate*)-1 == ate || NULL == ate)
      goto ERREXIT;
  }
  else {
    ate = (struct ate *)__ate;
  }

  /* check memory file to see if there is enough free memory to complete this
   * allocation. */
  for (;;) {
    c_pages = sbma_mtouch_probe(ate, __addr, __len);
    if (-1 == c_pages)
      goto CLEANUP;

    if (0 == c_pages)
      break;

    ret = ipc_madmit(&(_vmm_.ipc), c_pages, _vmm_.opts&VMM_ADMITD);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }

  numrd = sbma_mtouch_int(ate, __addr, __len);
  if (-1 == numrd)
    goto CLEANUP;

  if (NULL == __ate) {
    ret = lock_let(&(ate->lock));
    if (-1 == ret)
      goto CLEANUP;
  }

  /*========================================================================*/
  TIMER_STOP(&(tmr));
  SBMA_STATE_CHECK();
  /*========================================================================*/

  VMM_INTRA_CRITICAL_SECTION_BEG(&_vmm_);
  VMM_TRACK(&_vmm_, numrd, numrd);
  VMM_TRACK(&_vmm_, tmrrd, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);
  VMM_INTRA_CRITICAL_SECTION_END(&_vmm_);

  return c_pages;

  CLEANUP:
  if (NULL == __ate) {
    ret = lock_let(&(ate->lock));
    ASSERT(-1 != ret);
  }
  ERREXIT:
  return -1;
}


/****************************************************************************/
/*! Touch the specified ranges. */
/****************************************************************************/
SBMA_EXTERN ssize_t
sbma_mtouch_atomic(void * const __addr, size_t const __len, ...)
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
    _ate = mmu_lookup_ate(&(_vmm_.mmu), _addr);
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
        mnend_ = 1+((min_+mnlen_-mnate_->base-1)/_vmm_.page_size);
        mxbeg_ = (max_-mxate_->base)/_vmm_.page_size;

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
          ret = lock_let(&(_ate->lock));
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
      if (((VMM_AGGCH|VMM_LZYRD) != (_vmm_.opts&(VMM_AGGCH|VMM_LZYRD))) ||\
          (0 == dup[i]) || (0 != ate[i]->c_pages))
      {
        _c_pages = sbma_mtouch_probe(ate[i], addr[i], len[i]);
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

    ret = ipc_madmit(&(_vmm_.ipc), c_pages, _vmm_.opts&VMM_ADMITD);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }

  /* touch each of the pointers */
  for (numrd=0,i=0; i<num; ++i) {
    _numrd = sbma_mtouch_int(ate[i], addr[i], len[i]);
    if (-1 == _numrd)
      goto CLEANUP;
    numrd += _numrd;

    ret = lock_let(&(ate[i]->lock));
    if (-1 == ret)
      goto CLEANUP;

    ate[i] = NULL; /* clear in case of failure */
  }

  /*========================================================================*/
  TIMER_STOP(&(tmr));
  SBMA_STATE_CHECK();
  /*========================================================================*/

  VMM_INTRA_CRITICAL_SECTION_BEG(&_vmm_);
  VMM_TRACK(&_vmm_, numrd, numrd);
  VMM_TRACK(&_vmm_, tmrrd, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);
  VMM_INTRA_CRITICAL_SECTION_END(&_vmm_);

  return c_pages;

  CLEANUP:
  for (i=0; i<num; ++i) {
    if (NULL != ate[i]) {
      ret = lock_let(&(ate[i]->lock));
      ASSERT(-1 != ret);
    }
  }
  return -1;
}


/****************************************************************************/
/*! Touch all allocations. */
/****************************************************************************/
SBMA_EXTERN ssize_t
sbma_mtouchall(void)
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

  ret = lock_get(&(_vmm_.lock));
  if (-1 == ret)
    goto ERREXIT;

  /* Lock all allocations */
  for (ate=_vmm_.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = lock_get(&(ate->lock));
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
    for (c_pages=0,ate=_vmm_.mmu.a_tbl; NULL!=ate; ate=ate->next) {
      retval = sbma_mtouch_probe(ate, (void*)ate->base,\
        ate->n_pages*_vmm_.page_size);
      if (-1 == retval)
        goto CLEANUP;
      c_pages += retval;
    }

    if (0 == c_pages)
      break;

    ret = ipc_madmit(&(_vmm_.ipc), c_pages, _vmm_.opts&VMM_ADMITD);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }

  /* touch the memory */
  for (numrd=0,ate=_vmm_.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    retval = sbma_mtouch_int(ate, (void*)ate->base,\
      ate->n_pages*_vmm_.page_size);
    if (-1 == retval)
      goto CLEANUP;
    ASSERT(ate->l_pages == ate->n_pages);
    ASSERT(ate->c_pages == ate->n_pages);

    ret = lock_let(&(ate->lock));
    if (-1 == ret)
      goto CLEANUP;
    numrd += retval;

    start = ate->next;
  }

  ret = lock_let(&(_vmm_.lock));
  if (-1 == ret)
    goto CLEANUP;

  /*========================================================================*/
  TIMER_STOP(&(tmr));
  SBMA_STATE_CHECK();
  /*========================================================================*/

  VMM_INTRA_CRITICAL_SECTION_BEG(&_vmm_);
  VMM_TRACK(&_vmm_, numrd, numrd);
  VMM_TRACK(&_vmm_, tmrrd, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);
  VMM_INTRA_CRITICAL_SECTION_END(&_vmm_);

  return c_pages;

  CLEANUP:
  for (ate=start; stop!=ate; ate=ate->next) {
    ret = lock_let(&(ate->lock));
    ASSERT(-1 != ret);
  }
  ret = lock_let(&(_vmm_.lock));
  ASSERT(-1 != ret);
  ERREXIT:
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
