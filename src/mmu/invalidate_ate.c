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


#include <stddef.h> /* NULL */
#include "common.h"
#include "lock.h"
#include "mmu.h"


/*****************************************************************************/
/*  MT-Unsafe race:ate->*                                                    */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  This function is only called from malloc(), realloc() and free(),  */
/*        which are known to be MT-Unsafe.                                   */
/*****************************************************************************/
SBMA_EXTERN int
mmu_invalidate_ate(struct mmu * const mmu, struct ate * const ate)
{
  int retval;

  /* Acquire mmu lock. */
  retval = lock_get(&(mmu->lock));
  ERRCHK(RETURN, 0 != retval);

  /* Remove from doubly linked list. */
  if (NULL == ate->prev)
    mmu->a_tbl = ate->next;
  else
    ate->prev->next = ate->next;
  if (NULL != ate->next)
    ate->next->prev = ate->prev;

  /* Release mmu lock. */
  retval = lock_let(&(mmu->lock));
  ERRCHK(FATAL, 0 != retval);

  /***************************************************************************/
  /* Successful exit -- return 0. */
  /***************************************************************************/
  goto RETURN;

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
  FATAL_ABORT(retval);
}
