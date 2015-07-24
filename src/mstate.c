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

  if (((VMM_LZYRD|VMM_AGGCH) == (vmm.opts&(VMM_AGGCH|VMM_LZYRD))) &&\
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
  size_t i, numrd, beg, end, page_size;

  if (((VMM_LZYRD|VMM_AGGCH) == (vmm.opts&(VMM_AGGCH|VMM_LZYRD))) &&\
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
                    size_t const __len)
{
  size_t ip, beg, end, page_size, c_pages;
  volatile uint8_t * flags;

  page_size = vmm.page_size;
  flags     = __ate->flags;

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)__addr-__ate->base)/page_size;
  end = 1+(((uintptr_t)__addr+__len-__ate->base-1)/page_size);

  for (c_pages=0,ip=beg; ip<end; ++ip) {
    if (MMU_RSDNT != (flags[ip]&MMU_RSDNT)) { /* is resident */
      ASSERT(MMU_CHRGD != (flags[ip]&MMU_CHRGD)); /* is charged */
      c_pages++;
    }
  }

  return VMM_TO_SYS(c_pages);
}


/****************************************************************************/
/*! Internal: Evict the allocation containing addr. */
/****************************************************************************/
SBMA_STATIC ssize_t
__sbma_mevict_int(struct ate * const __ate, void * const __addr,
                  size_t const __len)
{
  size_t numwr, beg, end, page_size;

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
/*! Check to make sure that the state of the vmm is consistent. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_check(char const * const __func, int const __line)
{
  int ret, retval=0;
  size_t c_pages=0, s_pages, f_pages;
  struct ate * ate;

  ret = __lock_get(&(vmm.lock));
  if (-1 == ret)
    goto CLEANUP1;

  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = __lock_get(&(ate->lock));
    if (-1 == ret)
      goto CLEANUP2;

    s_pages  = 1+((sizeof(struct ate)-1)/vmm.page_size);
    f_pages  = 1+((ate->n_pages*sizeof(uint8_t)-1)/vmm.page_size);
    c_pages += s_pages+ate->c_pages+f_pages;

    ret = __lock_let(&(ate->lock));
    if (-1 == ret)
      goto CLEANUP2;
  }

  if (VMM_TO_SYS(c_pages) != vmm.ipc.c_mem[vmm.ipc.id]) {
    printf("[%5d] %s:%d c_pages (%zu) != c_mem[id] (%zu)\n", (int)getpid(),
      __func, __line, VMM_TO_SYS(c_pages), vmm.ipc.c_mem[vmm.ipc.id]);
    retval = -1;
  }

  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    goto CLEANUP1;

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
  ssize_t c_pages, numrd=0, chk_c_pages;
  struct ate * ate;

  ret = __sbma_check(__func__, __LINE__);
  ASSERT(-1 != ret);

  if (NULL != __ate) {
    ate = (struct ate *)__ate;
  }
  else {
    ate = __mmu_lookup_ate(&(vmm.mmu), __addr);
    if (NULL == ate)
      goto ERREXIT;
  }

  /* check memory file to see if there is enough free memory to complete this
   * allocation. */
  for (;;) {
    c_pages = __sbma_mtouch_probe(ate, __addr, __len);
    if (-1 == c_pages)
      goto CLEANUP;

    chk_c_pages = VMM_TO_SYS(ate->c_pages);

    if (0 == c_pages)
      break;

    ret = __ipc_madmit(&(vmm.ipc), c_pages);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }

  numrd = __sbma_mtouch_int(ate, __addr, __len);
  if (-1 == numrd)
    goto CLEANUP;

  ASSERT(chk_c_pages+c_pages == VMM_TO_SYS(ate->c_pages));

  if (NULL == __ate) {
    ret = __lock_let(&(ate->lock));
    if (-1 == ret)
      goto CLEANUP;
  }

  VMM_TRACK(numrd, numrd);

  ret = __sbma_check(__func__, __LINE__);
  ASSERT(-1 != ret);

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
  size_t i, num, _len, c_pages, mnlen_, mxlen_;
  ssize_t _c_pages, _numrd, numrd;
  uintptr_t min_, max_;
  va_list args;
  void * _addr;
  int dup[SBMA_ATOMIC_MAX];
  size_t len[SBMA_ATOMIC_MAX];
  void * addr[SBMA_ATOMIC_MAX];
  struct ate * _ate, * ate[SBMA_ATOMIC_MAX];

  size_t old_c_mem, chk_c_mem, new_c_mem;
  size_t old_c_pages, chk_c_pages, new_c_pages;
  size_t old_l_pages, chk_l_pages, new_l_pages;
  size_t atomic_old_c_pages, atomic_new_c_pages;
  size_t atomic_old_l_pages, atomic_new_l_pages;
  int a_status[512]={0};
  size_t a_old_c_pages[512]={0}, a_chk_c_pages[512]={0}, a_new_c_pages[512]={0};
  size_t a_old_l_pages[512]={0}, a_chk_l_pages[512]={0}, a_new_l_pages[512]={0};

  if (NULL == __addr)
    return 0;

  if (1) {
    int ret;
    size_t _c_pages=0, s_pages, f_pages;
    struct ate * _ate;

    ret = __sbma_check(__func__, __LINE__);
    ASSERT(-1 != ret);
    ASSERT(0 == __ipc_eligible(&(vmm.ipc)));

    ret = __lock_get(&(vmm.lock));
    if (-1 == ret)
      goto CLEANUP11;

    for (i=0,_ate=vmm.mmu.a_tbl; NULL!=_ate; _ate=_ate->next,++i) {
      ret = __lock_get(&(_ate->lock));
      if (-1 == ret)
        goto CLEANUP12;

      s_pages   = 1+((sizeof(struct ate)-1)/vmm.page_size);
      f_pages   = 1+((_ate->n_pages*sizeof(uint8_t)-1)/vmm.page_size);
      _c_pages += s_pages+_ate->c_pages+f_pages;

      a_old_l_pages[i] = VMM_TO_SYS(_ate->l_pages);
      a_old_c_pages[i] = VMM_TO_SYS(_ate->c_pages);

      ret = __lock_let(&(_ate->lock));
      if (-1 == ret)
        goto CLEANUP12;
    }
    old_c_pages = VMM_TO_SYS(_c_pages);
    old_c_mem   = vmm.ipc.c_mem[vmm.ipc.id];

    ret = __lock_let(&(vmm.lock));
    if (-1 == ret)
      goto CLEANUP11;

    goto DONE1;

    CLEANUP12:
    ret = __lock_let(&(_ate->lock));
    ASSERT(-1 != ret);
    CLEANUP11:
    ret = __lock_let(&(vmm.lock));
    ASSERT(-1 != ret);
    ASSERT(0);
    DONE1:
    (void)0;
  }

  /* populate the arrays with the variable number of pointers and lengths */
  num   = 0;
  _addr = __addr;
  _len  = __len;
  va_start(args, __len);
  while (SBMA_ATOMIC_END != _addr) {
    _ate = __mmu_lookup_ate(&(vmm.mmu), _addr);

    for (i=0; i<num; ++i) {
      if (_ate == ate[i]) {
        min_ = (uintptr_t)(_addr < addr[i] ? _addr : addr[i]);
        max_ = (uintptr_t)(_addr > addr[i] ? _addr : addr[i]);
        mnlen_ = _addr < addr[i] ? _len : len[i];
        mxlen_ = _addr > addr[i] ? _len : len[i];

        /* overlapping ranges of _ate */
        if (min_+mnlen_ >= max_) {
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

    _addr = va_arg(args, void *);
    if (SBMA_ATOMIC_END != _addr)
      _len = va_arg(args, size_t);
  }
  va_end(args);

  /* check memory file to see if there is enough free memory to admit the
   * required amount of memory. */
  for (;;) {
    atomic_old_l_pages = 0;
    atomic_old_c_pages = 0;
    for (c_pages=0,i=0; i<num; ++i) {
      _c_pages = __sbma_mtouch_probe(ate[i], addr[i], len[i]);
      if (-1 == _c_pages)
        goto CLEANUP;

      if (0 == dup[i]) {
        atomic_old_l_pages += VMM_TO_SYS(ate[i]->l_pages);
        atomic_old_c_pages += VMM_TO_SYS(ate[i]->c_pages);
      }

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
      if (((VMM_LZYRD|VMM_AGGCH) != (vmm.opts&(VMM_LZYRD|VMM_AGGCH))) ||\
          (0 == dup[i]) || (0 != ate[i]->c_pages))
      {
        c_pages += _c_pages;
      }
    }

    if (1) {
      int ret;
      size_t _c_pages=0, s_pages, f_pages;
      struct ate * _ate;

      ret = __lock_get(&(vmm.lock));
      if (-1 == ret)
        goto CLEANUP21;

      for (i=0,_ate=vmm.mmu.a_tbl; NULL!=_ate; _ate=_ate->next,++i) {
        ret = __lock_get(&(_ate->lock));
        if (-1 == ret)
          goto CLEANUP22;

        s_pages   = 1+((sizeof(struct ate)-1)/vmm.page_size);
        f_pages   = 1+((_ate->n_pages*sizeof(uint8_t)-1)/vmm.page_size);
        _c_pages += s_pages+_ate->c_pages+f_pages;

        a_chk_l_pages[i] = VMM_TO_SYS(_ate->l_pages);
        a_chk_c_pages[i] = VMM_TO_SYS(_ate->c_pages);

        ret = __lock_let(&(_ate->lock));
        if (-1 == ret)
          goto CLEANUP22;
      }
      chk_c_pages = VMM_TO_SYS(_c_pages);
      chk_c_mem   = vmm.ipc.c_mem[vmm.ipc.id];

      ret = __lock_let(&(vmm.lock));
      if (-1 == ret)
        goto CLEANUP21;

      goto DONE2;

      CLEANUP22:
      ret = __lock_let(&(_ate->lock));
      ASSERT(-1 != ret);
      CLEANUP21:
      ret = __lock_let(&(vmm.lock));
      ASSERT(-1 != ret);
      ASSERT(0);
      DONE2:
      (void)0;
    }

    if (0 == c_pages)
      break;

    ret = __ipc_madmit(&(vmm.ipc), c_pages);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }
  ASSERT(0 == __ipc_eligible(&(vmm.ipc)));
  ASSERT(0 == ipc_sigrecvd);

  /* touch each of the pointers */
  atomic_new_l_pages = 0;
  atomic_new_c_pages = 0;
  for (numrd=0,i=0; i<num; ++i) {
    _numrd = __sbma_mtouch_int(ate[i], addr[i], len[i]);
    if (-1 == _numrd)
      goto CLEANUP;
    numrd += _numrd;

    if (0 == dup[i]) {
      atomic_new_l_pages += VMM_TO_SYS(ate[i]->l_pages);
      atomic_new_c_pages += VMM_TO_SYS(ate[i]->c_pages);
    }

    ret = __lock_let(&(ate[i]->lock));
    if (-1 == ret)
      goto CLEANUP;

    //ate[i] = NULL; /* clear in case of failure */
  }

  VMM_TRACK(numrd, numrd);

  if (1) {
    int ret;
    size_t ii, _c_pages=0, s_pages, f_pages;
    struct ate * _ate;

    ret = __lock_get(&(vmm.lock));
    if (-1 == ret)
      goto CLEANUP31;

    for (i=0,_ate=vmm.mmu.a_tbl; NULL!=_ate; _ate=_ate->next,++i) {
      ret = __lock_get(&(_ate->lock));
      if (-1 == ret)
        goto CLEANUP32;

      s_pages   = 1+((sizeof(struct ate)-1)/vmm.page_size);
      f_pages   = 1+((_ate->n_pages*sizeof(uint8_t)-1)/vmm.page_size);
      _c_pages += s_pages+_ate->c_pages+f_pages;

      a_new_l_pages[i] = VMM_TO_SYS(_ate->l_pages);
      a_new_c_pages[i] = VMM_TO_SYS(_ate->c_pages);

      for (ii=0; ii<num; ++ii) {
        if (_ate == ate[ii]) {
          a_status[i] = 1;
          break;
        }
      }

      ret = __lock_let(&(_ate->lock));
      if (-1 == ret)
        goto CLEANUP32;
    }
    new_c_pages = VMM_TO_SYS(_c_pages);
    new_c_mem   = vmm.ipc.c_mem[vmm.ipc.id];

    ret = __lock_let(&(vmm.lock));
    if (-1 == ret)
      goto CLEANUP31;

    goto DONE3;

    CLEANUP32:
    ret = __lock_let(&(_ate->lock));
    ASSERT(-1 != ret);
    CLEANUP31:
    ret = __lock_let(&(vmm.lock));
    ASSERT(-1 != ret);
    ASSERT(0);
    DONE3:
    (void)0;
  }
  if (1) {
    if ((chk_c_pages+c_pages != new_c_pages) ||\
        (chk_c_mem+c_pages != new_c_mem) ||\
        (new_c_pages != new_c_mem))
    {
      printf("[%5d] inconsistent state@%s:%d\n", (int)getpid(), __func__,\
        __LINE__);
      printf("  touch ranges:            ");
      for (i=0; i<num; ++i) {
        printf(" <%d>[%zu..%zu)", dup[i], (uintptr_t)addr[i],\
          (uintptr_t)addr[i]+len[i]);
      }
      printf("\n");
      printf("  c_mem   (before):         %zu\n", old_c_mem);
      printf("  c_pages (before):         %zu\n", old_c_pages);
      printf("  l_pages (before):         %zu\n", old_l_pages);
      printf("  l_pages (atomic/before):  %zu\n", atomic_old_l_pages);
      printf("  c_pages (atomic/before):  %zu\n", atomic_old_c_pages);
      printf("  c_mem   (chkpt):          %zu\n", chk_c_mem);
      printf("  c_pages (chkpt):          %zu\n", chk_c_pages);
      printf("  l_pages (chkpt):          %zu\n", chk_l_pages);
      printf("  pages to be charged:      %zu\n", c_pages);
      printf("  c_mem   (after):          %zu\n", new_c_mem);
      printf("  c_pages (after):          %zu\n", new_c_pages);
      printf("  l_pages (after):          %zu\n", new_l_pages);
      printf("  l_pages (atomic/after):   %zu\n", atomic_new_l_pages);
      printf("  c_pages (atomic/after):   %zu\n", atomic_new_c_pages);
      printf("  per alloc c_pages:       ");
      for (i=0,_ate=vmm.mmu.a_tbl; NULL!=_ate; _ate=_ate->next,++i) {
        printf(" (%d/%zu,%zu,%zu/%zu,%zu,%zu)", a_status[i],\
          a_old_l_pages[i], a_chk_l_pages[i], a_new_l_pages[i],\
          a_old_c_pages[i], a_chk_c_pages[i], a_new_c_pages[i]);
      }
      printf("\n");
      ASSERT(0);
    }
  }

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
  struct ate * ate, * start=NULL, * stop=NULL;

  ret = __lock_get(&(vmm.lock));
  if (-1 == ret)
    goto ERREXIT;

  ret = __sbma_check(__func__, __LINE__);
  ASSERT(-1 != ret);

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

    ret = __ipc_madmit(&(vmm.ipc), c_pages);
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

  VMM_TRACK(numrd, numrd);

  ret = __sbma_check(__func__, __LINE__);
  ASSERT(-1 != ret);

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
  size_t beg, end, page_size;
  ssize_t ret;
  struct ate * ate;

  ret = __sbma_check(__func__, __LINE__);
  ASSERT(-1 != ret);

  ate = __mmu_lookup_ate(&(vmm.mmu), __addr);
  if (NULL == ate)
    goto ERREXIT;

  page_size = vmm.page_size;

  /* can only clear pages fully within range, thus beg is a ceil
   * operation and end is a floor operation, except for when addr+len
   * consumes all of the last page, then end just equals n_pages. */
  beg = 1+(((uintptr_t)__addr-ate->base-1)/page_size);
  end = ((uintptr_t)__addr+__len-ate->base)/page_size;

  if (beg <= end) {
    ret = __vmm_swap_x(ate, beg, end-beg);
    if (-1 == ret)
      goto CLEANUP;
  }

  ret = __lock_let(&(ate->lock));
  if (-1 == ret)
    goto CLEANUP;

  ret = __sbma_check(__func__, __LINE__);
  ASSERT(-1 != ret);

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
  ssize_t ret;
  struct ate * ate;

  ret = __sbma_check(__func__, __LINE__);
  ASSERT(-1 != ret);

  ret = __lock_get(&(vmm.lock));
  if (-1 == ret)
    goto ERREXIT;

  for (ate=vmm.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = __sbma_mclear((void*)ate->base, ate->n_pages*vmm.page_size);
    if (-1 == ret)
      goto CLEANUP;
  }

  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    goto CLEANUP;

  ret = __sbma_check(__func__, __LINE__);
  ASSERT(-1 != ret);

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
  ssize_t c_pages, numwr;
  struct ate * ate;

  ret = __sbma_check(__func__, __LINE__);
  ASSERT(-1 != ret);

  ate = __mmu_lookup_ate(&(vmm.mmu), __addr);
  if (NULL == ate)
    goto ERREXIT;

  c_pages = __sbma_mevict_probe(ate, __addr, __len);
  if (-1 == c_pages)
    goto CLEANUP;

  numwr = __sbma_mevict_int(ate, __addr, __len);
  if (-1 == numwr)
    goto CLEANUP;

  /* update memory file */
  for (;;) {
    ret = __ipc_mevict(&(vmm.ipc), c_pages);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }

  VMM_TRACK(numwr, numwr);

  ret = __sbma_check(__func__, __LINE__);
  ASSERT(-1 != ret);

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
__sbma_mevictall_int(size_t * const __c_pages, size_t * const __numwr)
{
  size_t c_pages=0, numwr=0;
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
    ret = __sbma_mevict_int(ate, (void*)ate->base,\
      ate->n_pages*vmm.page_size);
    if (-1 == ret)
      goto CLEANUP2;
    numwr += ret;
    ASSERT(0 == ate->l_pages);
    ASSERT(0 == ate->c_pages);
    ret = __lock_let(&(ate->lock));
    if (-1 == ret)
      goto CLEANUP2;
  }

  ret = __lock_let(&(vmm.lock));
  if (-1 == ret)
    goto CLEANUP1;

  *__c_pages = VMM_TO_SYS(c_pages);
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
__sbma_mevictall_int(size_t * const __c_pages, size_t * const __numwr));


/****************************************************************************/
/*! Evict all allocations. */
/****************************************************************************/
SBMA_EXTERN ssize_t
__sbma_mevictall(void)
{
  int ret;
  size_t c_pages, numwr;

  ret = __sbma_check(__func__, __LINE__);
  ASSERT(-1 != ret);

  ret = __sbma_mevictall_int(&c_pages, &numwr);
  if (-1 == ret)
    goto ERREXIT;

  /* update memory file */
  for (;;) {
    ret = __ipc_mevict(&(vmm.ipc), c_pages);
    if (-1 == ret)
      goto ERREXIT;
    else if (-2 != ret)
      break;
  }

  /* change my status to unpopulated - must be before any potential waiting,
   * since SIGIPC could be raised again then. */
  __ipc_unpopulate(&(vmm.ipc));

  VMM_TRACK(numwr, numwr);

  ret = __sbma_check(__func__, __LINE__);
  ASSERT(-1 != ret);

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
