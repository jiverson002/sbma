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


#ifndef SBAM_MMU_H
#define SBAM_MMU_H 1


#include <stdint.h> /* uint8_t, uintptr_t */


/*****************************************************************************/
/*
 *  Memory management unit page status code bits:
 *
 *    bit 0 ==    0: zero fill allowed       1: page must be filled from disk
 *    bit 1 ==    0: page is resident        1: page is not resident
 *    bit 2 ==    0: page is unmodified      1: page is dirty
 *    bit 3 ==    0: page has been charged   1: page is uncharged
 */
/*****************************************************************************/
enum mmu_status_code
{
  MMU_ZFILL = 1 << 0,
  MMU_RSDNT = 1 << 1,
  MMU_DIRTY = 1 << 2,
  MMU_CHRGD = 1 << 3
};


/*****************************************************************************/
/*  Allocation table entry. */
/*****************************************************************************/
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


/*****************************************************************************/
/*  Memory management unit. */
/*****************************************************************************/
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

/*****************************************************************************/
/*  Initialize the memory management unit. */
/*****************************************************************************/
int
mmu_init(struct mmu * const mmu, size_t const page_size);


/*****************************************************************************/
/*  Destroy the memory management unit. */
/*****************************************************************************/
int
mmu_destroy(struct mmu * const mmu);


/*****************************************************************************/
/*  Insert ate into mmu. */
/*****************************************************************************/
int
mmu_insert_ate(struct mmu * const mmu, struct ate * const ate);


/*****************************************************************************/
/*  Invalidate ate. */
/*****************************************************************************/
int
mmu_invalidate_ate(struct mmu * const mmu, struct ate * const ate);


/*****************************************************************************/
/*  Find the ate, if one exists, that contains addr. */
/*****************************************************************************/
struct ate *
mmu_lookup_ate(struct mmu * const mmu, void const * const addr);

#ifdef __cplusplus
}
#endif


#endif /* SBMA_MMU_H */
