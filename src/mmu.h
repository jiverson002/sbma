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

#ifndef __MMU_H__
#define __MMU_H__ 1


#include <stdint.h> /* uint8_t, uintptr_t */


/****************************************************************************/
/*!
 * Memory management unit page status code bits:
 *
 *   bit 0 ==    0: zero fill allowed       1: page must be filled from disk
 *   bit 1 ==    0: page is resident        1: page is not resident
 *   bit 2 ==    0: page is unmodified      1: page is dirty
 *   bit 3 ==    0: page has been charged   1: page is uncharged
 */
/****************************************************************************/
enum mmu_status_code
{
  MMU_ZFILL = 1 << 0,
  MMU_RSDNT = 1 << 1,
  MMU_DIRTY = 1 << 2,
  MMU_CHRGD = 1 << 3
};


/****************************************************************************/
/*! Allocation table entry. */
/****************************************************************************/
struct ate
{
  size_t n_pages;           /*!< number of pages allocated */
  volatile size_t l_pages;  /*!< number of pages loaded */
  volatile size_t c_pages;  /*!< number of pages charged */
  volatile size_t d_pages;  /*!< number of pages dirty */
  uintptr_t base;           /*!< starting address fro the allocation */
  volatile uint8_t * flags; /*!< status flags for pages */
  struct ate * prev;        /*!< doubly linked list pointer */
  struct ate * next;        /*!< doubly linked list pointer */
#ifdef USE_THREAD
  pthread_mutex_t lock;     /*!< mutex guarding struct */
#endif
};


/****************************************************************************/
/*! Memory management unit. */
/****************************************************************************/
struct mmu
{
  size_t page_size;     /*!< page size */
  struct ate * a_tbl;   /*!< mmu allocation table */
#ifdef USE_THREAD
  pthread_mutex_t lock; /*!< mutex guarding struct */
#endif
};


#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************/
/*! Initialize the memory management unit. */
/****************************************************************************/
int
__mmu_init(struct mmu * const __mmu, size_t const __page_size);


/****************************************************************************/
/*! Destroy the memory management unit. */
/****************************************************************************/
int
__mmu_destroy(struct mmu * const __mmu);


/****************************************************************************/
/*! Insert __ate into __mmu. */
/****************************************************************************/
int
__mmu_insert_ate(struct mmu * const __mmu, struct ate * const __ate);


/****************************************************************************/
/*! Invalidate __ate. */
/****************************************************************************/
int
__mmu_invalidate_ate(struct mmu * const __mmu, struct ate * const __ate);


/****************************************************************************/
/*! Find the ate, if one exists, that contains __addr. */
/****************************************************************************/
struct ate *
__mmu_lookup_ate(struct mmu * const __mmu, void const * const __addr);

#ifdef __cplusplus
}
#endif


#endif
