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
#include "common.h"
#include "ipc.h"
#include "lock.h"
#include "mmu.h"
#include "sbma.h"
#include "vmm.h"


/****************************************************************************/
/*! Count the number of dirty pages to be cleared by a clear operation. */
/****************************************************************************/
SBMA_STATIC ssize_t
sbma_mclear_probe(struct ate * const __ate, void * const __addr,
                  size_t const __len, size_t * const __d_pages)
{
  size_t ip, beg, end, page_size, d_pages;
  volatile uint8_t * flags;

  page_size = _vmm_.page_size;
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
sbma_mclear_int(struct ate * const __ate, void * const __addr,
                size_t const __len)
{
  size_t beg, end, page_size;
  ssize_t ret;

  page_size = _vmm_.page_size;

  /* can only clear pages fully within range, thus beg is a ceil
   * operation and end is a floor operation, except for when addr+len
   * consumes all of the last page, then end just equals n_pages. */
  if ((uintptr_t)__addr == __ate->base)
    beg = 0;
  else
    beg = 1+(((uintptr_t)__addr-__ate->base-1)/page_size);
  end = ((uintptr_t)__addr+__len-__ate->base)/page_size;

  if (beg <= end) {
    ret = vmm_swap_x(__ate, beg, end-beg);
    if (-1 == ret)
      return -1;
  }

  return 0;
}


/****************************************************************************/
/*! Clear the specified range. */
/****************************************************************************/
SBMA_EXTERN ssize_t
sbma_mclear(void * const __addr, size_t const __len)
{
  size_t d_pages;
  ssize_t ret;
  struct ate * ate;

  SBMA_STATE_CHECK();

  ate = mmu_lookup_ate(&(_vmm_.mmu), __addr);
  if ((struct ate*)-1 == ate || NULL == ate)
    goto ERREXIT;

  ret = sbma_mclear_probe(ate, __addr, __len, &d_pages);
  if (-1 == ret)
    goto CLEANUP;

  ret = sbma_mclear_int(ate, __addr, __len);
  if (-1 == ret)
    goto CLEANUP;

  /* update memory file */
  /* TODO can this be outside of lock_let? */
  for (;;) {
    ret = ipc_mevict(&(_vmm_.ipc), 0, d_pages);
    if (-1 == ret)
      goto CLEANUP;
    else if (-2 != ret)
      break;
  }

  ret = lock_let(&(ate->lock));
  if (-1 == ret)
    goto CLEANUP;

  SBMA_STATE_CHECK();
  return 0;

  CLEANUP:
  ret = lock_let(&(ate->lock));
  ASSERT(-1 != ret);
  ERREXIT:
  return -1;
}


/****************************************************************************/
/*! Clear all allocations. */
/****************************************************************************/
SBMA_EXTERN ssize_t
sbma_mclearall(void)
{
  size_t d_pages=0;
  ssize_t ret;
  struct ate * ate;

  SBMA_STATE_CHECK();

  ret = lock_get(&(_vmm_.lock));
  if (-1 == ret)
    goto ERREXIT;

  for (ate=_vmm_.mmu.a_tbl; NULL!=ate; ate=ate->next) {
    d_pages += ate->d_pages;

    ret = sbma_mclear((void*)ate->base, ate->n_pages*_vmm_.page_size);
    if (-1 == ret)
      goto CLEANUP;

    ASSERT(0 == ate->d_pages);
  }

  ret = lock_let(&(_vmm_.lock));
  if (-1 == ret)
    goto CLEANUP;

  ret = ipc_mdirty(&(_vmm_.ipc), -VMM_TO_SYS(d_pages));
  if (-1 == ret)
    goto CLEANUP;

  SBMA_STATE_CHECK();
  return 0;

  CLEANUP:
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
