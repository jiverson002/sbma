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


#include <stdint.h> /* uintptr_t */
#include <stddef.h> /* NULL */
#include "common.h"
#include "lock.h"
#include "mmu.h"


SBMA_EXTERN int
__mmu_init(struct mmu * const __mmu, size_t const __page_size)
{
  int retval;

  /* clear pointer */
  __mmu->a_tbl = NULL;

  /* set mmu page size */
  __mmu->page_size = __page_size;

  /* initialize mmu lock */
  retval = __lock_init(&(__mmu->lock));
  ERRCHK(RETURN, 0 != retval);

  RETURN:
  return retval;
}
SBMA_EXPORT(internal, int
__mmu_init(struct mmu * const __mmu, size_t const __page_size));


SBMA_EXTERN int
__mmu_destroy(struct mmu * const __mmu)
{
  int retval;

  /* destroy mmu lock */
  retval = __lock_free(&(__mmu->lock));
  ERRCHK(RETURN, 0 != retval);

  RETURN:
  return retval;

  if (NULL == __mmu) {}
}
SBMA_EXPORT(internal, int
__mmu_destroy(struct mmu * const __mmu));


SBMA_EXTERN int
__mmu_insert_ate(struct mmu * const __mmu, struct ate * const __ate)
{
  int retval;

  /* Acquire lock */
  retval = __lock_get(&(__mmu->lock));
  ERRCHK(RETURN, 0 != retval);

  /* Insert at beginning of doubly linked list */
  if (NULL == __mmu->a_tbl) {
    __mmu->a_tbl = __ate;
    __ate->prev  = NULL;
    __ate->next  = NULL;
  }
  else {
    __ate->prev        = __mmu->a_tbl->prev;
    __ate->next        = __mmu->a_tbl;
    __mmu->a_tbl->prev = __ate;
    __mmu->a_tbl       = __ate;
  }

  /* Release lock */
  retval = __lock_let(&(__mmu->lock));
  ERRCHK(FATAL, 0 != retval);

  /**************************************************************************/
  /* Successful exit -- return 0. */
  /**************************************************************************/
  goto RETURN;

  /**************************************************************************/
  /* Return point -- return. */
  /**************************************************************************/
  RETURN:
  return retval;

  /**************************************************************************/
  /* Fatal error -- an unrecoverable error has occured, the runtime state
   * cannot be reverted to its state before this function was called. */
  /**************************************************************************/
  FATAL:
  FATAL_ABORT(retval);
}
SBMA_EXPORT(internal, int
__mmu_insert_ate(struct mmu * const __mmu, struct ate * const __ate));


SBMA_EXTERN int
__mmu_invalidate_ate(struct mmu * const __mmu, struct ate * const __ate)
{
  int retval;

  /* Acquire lock */
  retval = __lock_get(&(__mmu->lock));
  ERRCHK(RETURN, 0 != retval);

  /* Remove from doubly linked list. */
  if (NULL == __ate->prev)
    __mmu->a_tbl = __ate->next;
  else
    __ate->prev->next = __ate->next;
  if (NULL != __ate->next)
    __ate->next->prev = __ate->prev;

  /* Release lock */
  retval = __lock_let(&(__mmu->lock));
  ERRCHK(FATAL, 0 != retval);

  /**************************************************************************/
  /* Successful exit -- return 0. */
  /**************************************************************************/
  goto RETURN;

  /**************************************************************************/
  /* Return point -- return. */
  /**************************************************************************/
  RETURN:
  return retval;

  /**************************************************************************/
  /* Fatal error -- an unrecoverable error has occured, the runtime state
   * cannot be reverted to its state before this function was called. */
  /**************************************************************************/
  FATAL:
  FATAL_ABORT(retval);
}
SBMA_EXPORT(internal, int
__mmu_invalidate_ate(struct mmu * const __mmu, struct ate * const __ate));


SBMA_EXTERN struct ate *
__mmu_lookup_ate(struct mmu * const __mmu, void const * const __addr)
{
  int ret;
  size_t len;
  void * addr;
  struct ate * ate, * retval;

  /* Default return value. */
  retval = (struct ate*)-1;

  /* Acquire lock. */
  ret = __lock_get(&(__mmu->lock));
  ERRCHK(RETURN, 0 != ret);

  /* Search doubly linked list for a ate which contains __addr. */
  for (ate=__mmu->a_tbl; NULL!=ate; ate=ate->next) {
    len  = ate->n_pages*__mmu->page_size;
    addr = (void*)ate->base;
    if (addr <= __addr && __addr < (void*)((uintptr_t)addr+len))
      break;
  }

  /* Lock ate. */
  if (NULL != ate) {
    ret = __lock_get(&(ate->lock));
    ERRCHK(REVERT, 0 != ret);
  }

  /* Release lock. */
  ret = __lock_let(&(__mmu->lock));
  ERRCHK(FATAL, 0 != ret);

  /**************************************************************************/
  /* Successful exit -- return pointer to ate containing __addr. */
  /**************************************************************************/
  retval = ate;
  goto RETURN;

  /**************************************************************************/
  /* Error exit -- revert changes to runtime state and return. */
  /**************************************************************************/
  REVERT:
  ret = __lock_let(&(__mmu->lock));
  ERRCHK(FATAL, 0 != ret);

  /**************************************************************************/
  /* Return point -- return. */
  /**************************************************************************/
  RETURN:
  return retval;

  /**************************************************************************/
  /* Fatal error -- an unrecoverable error has occured, the runtime state
   * cannot be reverted to its state before this function was called. */
  /**************************************************************************/
  FATAL:
  FATAL_ABORT(ret);
}
SBMA_EXPORT(internal, struct ate *
__mmu_lookup_ate(struct mmu * const __mmu, void const * const __addr));
