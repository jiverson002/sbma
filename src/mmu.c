/*
Copyright (c) 2015 Jeremy Iverson

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
/*  MT-Invalid                                                               */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  This function MUST be called EXACTLY ONCE BEFORE any other mmu_*   */
/*        function is called.                                                */
/*****************************************************************************/
SBMA_EXTERN int
mmu_init(struct mmu * const mmu, size_t const page_size)
{
  int retval;

  /* Clear pointer. */
  mmu->a_tbl = NULL;

  /* Set mmu page size. */
  mmu->page_size = page_size;

  /* Initialize mmu lock. */
  retval = __lock_init(&(mmu->lock));
  ERRCHK(RETURN, 0 != retval);

  RETURN:
  return retval;
}
SBMA_EXPORT(internal, int
mmu_init(struct mmu * const mmu, size_t const page_size));


/*****************************************************************************/
/*  MT-Invalid                                                               */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  This function MUST be called EXACTLY ONCE AFTER all other ipc_*    */
/*        functions are called.                                              */
/*****************************************************************************/
SBMA_EXTERN int
mmu_destroy(struct mmu * const mmu)
{
  int retval;

  /* Destroy mmu lock. */
  retval = __lock_free(&(mmu->lock));
  ERRCHK(RETURN, 0 != retval);

  RETURN:
  return retval;
}
SBMA_EXPORT(internal, int
mmu_destroy(struct mmu * const mmu));


/*****************************************************************************/
/*  MT-Safe                                                                  */
/*****************************************************************************/
SBMA_EXTERN int
mmu_insert_ate(struct mmu * const mmu, struct ate * const ate)
{
  int retval;

  /* Acquire mmu lock. */
  retval = __lock_get(&(mmu->lock));
  ERRCHK(RETURN, 0 != retval);

  /* Insert at beginning of doubly linked list. */
  if (NULL == mmu->a_tbl) {
    mmu->a_tbl = ate;
    ate->prev  = NULL;
    ate->next  = NULL;
  }
  else {
    ate->prev        = mmu->a_tbl->prev;
    ate->next        = mmu->a_tbl;
    mmu->a_tbl->prev = ate;
    mmu->a_tbl       = ate;
  }

  /* Release mmu lock. */
  retval = __lock_let(&(mmu->lock));
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
SBMA_EXPORT(internal, int
mmu_insert_ate(struct mmu * const mmu, struct ate * const ate));


/*****************************************************************************/
/*  MT-Safe                                                                  */
/*****************************************************************************/
SBMA_EXTERN int
mmu_invalidate_ate(struct mmu * const mmu, struct ate * const ate)
{
  int retval;

  /* Acquire mmu lock. */
  retval = __lock_get(&(mmu->lock));
  ERRCHK(RETURN, 0 != retval);

  /* Remove from doubly linked list. */
  if (NULL == ate->prev)
    mmu->a_tbl = ate->next;
  else
    ate->prev->next = ate->next;
  if (NULL != ate->next)
    ate->next->prev = ate->prev;

  /* Release mmu lock. */
  retval = __lock_let(&(mmu->lock));
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
SBMA_EXPORT(internal, int
mmu_invalidate_ate(struct mmu * const mmu, struct ate * const ate));


/*****************************************************************************/
/*  MT-Safe                                                                  */
/*****************************************************************************/
SBMA_EXTERN struct ate *
mmu_lookup_ate(struct mmu * const mmu, void const * const addr)
{
  int ret;
  size_t len;
  void * addr;
  struct ate * ate, * retval;

  /* Default return value. */
  retval = (struct ate*)-1;

  /* Acquire mmu lock. */
  ret = __lock_get(&(mmu->lock));
  ERRCHK(RETURN, 0 != ret);

  /* Search doubly linked list for a ate which contains addr. */
  for (ate=mmu->a_tbl; NULL!=ate; ate=ate->next) {
    len  = ate->n_pages*mmu->page_size;
    addr = (void*)ate->base;
    if (addr <= addr && addr < (void*)((uintptr_t)addr+len))
      break;
  }

  /* Acquire ate lock. */
  if (NULL != ate) {
    ret = __lock_get(&(ate->lock));
    ERRCHK(REVERT, 0 != ret);
  }

  /* Release mmu lock. */
  ret = __lock_let(&(mmu->lock));
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
  ret = __lock_let(&(mmu->lock));
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
SBMA_EXPORT(internal, struct ate *
mmu_lookup_ate(struct mmu * const mmu, void const * const addr));
