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
/*! Count the number of pages to be discharged by a evict operation. */
/****************************************************************************/
SBMA_STATIC ssize_t
sbma_mevict_probe(struct ate * const __ate, void * const __addr,
                  size_t const __len, size_t * const __c_pages,
                  size_t * const __d_pages)
{
  size_t ip, beg, end, page_size, c_pages, d_pages;
  volatile uint8_t * flags;

  page_size = _vmm_.page_size;
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
sbma_mevict_int(struct ate * const __ate, void * const __addr,
                size_t const __len)
{
  size_t beg, end, page_size;
  ssize_t numwr;

  page_size = _vmm_.page_size;

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  beg = ((uintptr_t)__addr-__ate->base)/page_size;
  end = 1+(((uintptr_t)__addr+__len-__ate->base-1)/page_size);

  numwr = vmm_swap_o(__ate, beg, end-beg);
  if (-1 == numwr)
    return -1;

  return VMM_TO_SYS(numwr);
}


/****************************************************************************/
/*! Evict the allocation containing addr. */
/****************************************************************************/
SBMA_EXTERN ssize_t
sbma_mevict(void * const __addr, size_t const __len)
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

  ate = mmu_lookup_ate(&(_vmm_.mmu), __addr);
  if ((struct ate*)-1 == ate || NULL == ate)
    goto ERREXIT;

  ret = sbma_mevict_probe(ate, __addr, __len, &c_pages, &d_pages);
  if (-1 == ret)
    goto CLEANUP;

  numwr = sbma_mevict_int(ate, __addr, __len);
  if (-1 == numwr)
    goto CLEANUP;

  /* update memory file */
  /* TODO can this be outside of lock_let? */
  for (;;) {
    ret = ipc_mevict(&(_vmm_.ipc), c_pages, d_pages);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }

  ret = lock_let(&(ate->lock));
  if (-1 == ret)
    goto CLEANUP;

  /*========================================================================*/
  TIMER_STOP(&(tmr));
  SBMA_STATE_CHECK();
  /*========================================================================*/

  VMM_INTRA_CRITICAL_SECTION_BEG(&_vmm_);
  VMM_TRACK(&_vmm_, numwr, numwr);
  VMM_TRACK(&_vmm_, tmrwr, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);
  VMM_INTRA_CRITICAL_SECTION_END(&_vmm_);

  return c_pages;

  CLEANUP:
  ret = lock_let(&(ate->lock));
  ASSERT(-1 != ret);
  ERREXIT:
  return -1;
}


/****************************************************************************/
/*! Internal: Evict all allocations. */
/****************************************************************************/
SBMA_EXTERN int
sbma_mevictall_int(size_t * const __c_pages, size_t * const __d_pages,
                   size_t * const __numwr)
{
  size_t c_pages=0, d_pages=0, numwr=0;
  ssize_t ret;
  struct ate * ate;

  ret = lock_get(&(_vmm_.lock));
  if (-1 == ret)
    goto ERREXIT;

  for (ate=_vmm_.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    ret = lock_get(&(ate->lock));
    if (-1 == ret)
      goto CLEANUP1;
    c_pages += ate->c_pages;
    d_pages += ate->d_pages;
    ret = sbma_mevict_int(ate, (void*)ate->base,\
      ate->n_pages*_vmm_.page_size);
    if (-1 == ret)
      goto CLEANUP2;
    numwr += ret;
    ASSERT(0 == ate->l_pages);
    ASSERT(0 == ate->c_pages);
    ASSERT(0 == ate->d_pages);
    ret = lock_let(&(ate->lock));
    if (-1 == ret)
      goto CLEANUP2;
  }

  ret = lock_let(&(_vmm_.lock));
  if (-1 == ret)
    goto CLEANUP1;

  *__c_pages = VMM_TO_SYS(c_pages);
  *__d_pages = VMM_TO_SYS(d_pages);
  *__numwr   = VMM_TO_SYS(numwr);

  return 0;

  CLEANUP2:
  ret = lock_let(&(ate->lock));
  ASSERT(-1 != ret);
  CLEANUP1:
  ret = lock_let(&(_vmm_.lock));
  ASSERT(-1 != ret);
  ERREXIT:
  return -1;
}


/****************************************************************************/
/*! Evict all allocations. */
/****************************************************************************/
SBMA_EXTERN ssize_t
sbma_mevictall(void)
{
  int ret;
  size_t c_pages, d_pages, numwr;
  struct timespec tmr;

  /*========================================================================*/
  SBMA_STATE_CHECK();
  TIMER_START(&(tmr));
  /*========================================================================*/

  ret = sbma_mevictall_int(&c_pages, &d_pages, &numwr);
  if (-1 == ret)
    goto ERREXIT;

  /* update memory file */
  for (;;) {
    ret = ipc_mevict(&(_vmm_.ipc), c_pages, d_pages);
    if (-1 == ret)
      goto ERREXIT;
    else if (-2 != ret)
      break;
  }

  /*========================================================================*/
  TIMER_STOP(&(tmr));
  SBMA_STATE_CHECK();
  /*========================================================================*/

  VMM_INTRA_CRITICAL_SECTION_BEG(&_vmm_);
  VMM_TRACK(&_vmm_, numwr, numwr);
  VMM_TRACK(&_vmm_, tmrwr, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);
  VMM_INTRA_CRITICAL_SECTION_END(&_vmm_);

  return c_pages;

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
