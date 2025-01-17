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


#include <stdint.h> /* uintptr_t */
#include <stddef.h> /* NULL */
#include "common.h"
#include "lock.h"
#include "mmu.h"


/*****************************************************************************/
/*  MT-Safe                                                                  */
/*                                                                           */
/*  Note:                                                                    */ 
/*    1)  On success, this function will acquire, but not release the lock   */
/*        for an ate.                                                        */
/*****************************************************************************/
SBMA_EXTERN struct ate *
mmu_lookup_ate(struct mmu * const mmu, void const * const addr)
{
  int ret;
  size_t len;
  void * addr_;
  struct ate * ate, * retval;

  /* Default return value. */
  retval = (struct ate*)-1;

  /* Acquire mmu lock. */
  ret = lock_get(&(mmu->lock));
  ERRCHK(RETURN, 0 != ret);

  /* Search doubly linked list for a ate which contains addr. */
  for (ate=mmu->a_tbl; NULL!=ate; ate=ate->next) {
    len  = ate->n_pages*mmu->page_size;
    addr_ = (void*)ate->base;
    if (addr_ <= addr && addr < (void*)((uintptr_t)addr_+len))
      break;
  }

  /* Acquire ate lock. */
  if (NULL != ate) {
    ret = lock_get(&(ate->lock));
    ERRCHK(REVERT, 0 != ret);
  }

  /* Release mmu lock. */
  ret = lock_let(&(mmu->lock));
  ERRCHK(FATAL, 0 != ret);

  /***************************************************************************/
  /* Successful exit -- return pointer to ate containing addr. */
  /***************************************************************************/
  retval = ate;
  goto RETURN;

  /***************************************************************************/
  /* Error exit -- revert changes to runtime state and return. */
  /***************************************************************************/
  REVERT:
  /* Release mmu lock. */
  ret = lock_let(&(mmu->lock));
  ERRCHK(FATAL, 0 != ret);

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
  FATAL_ABORT(ret);
}


#ifdef TEST
int
main(int argc, char * argv[])
{
  if (0 == argc || NULL == argv) {}

  return 0;
}
#endif
