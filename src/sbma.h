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

#ifndef __SBMA_H__
#define __SBMA_H__ 1


#include <sys/types.h> /* ssize_t */


#define SBMA_MAJOR   0
#define SBMA_MINOR   2
#define SBMA_PATCH   5
//#define SBMA_RCAND   -pre
#define SBMA_VERSION (SBMA_MAJOR*10000+SBMA_MINOR*100+SBMA_PATCH)

#define SBMA_DEFAULT_PAGE_SIZE (1<<14)
#define SBMA_DEFAULT_FSTEM     "/tmp/"
#define SBMA_DEFAULT_OPTS      0

#define SBMA_ATOMIC_MAX 256
#define SBMA_ATOMIC_END ((void*)-1)


/****************************************************************************/
/*! Mallopt parameters. */
/****************************************************************************/
enum __sbma_mallopt_params
{
  M_VMMOPTS = 0 /*!< vmm option parameter for mallopt */
};


/****************************************************************************/
/*!
 * Virtual memory manager option bits:
 *
 *   bit  0 ==    0: page default evict   1: page default resident
 *   bit  1 ==    0: lazy read            1: aggresive read
 *   bit  2 ==    0:                      1: aggressive charge (meaningful w/ lazy read)
 *   bit  3 ==    0:                      1: ghost pages
 *   bit  4 ==    0:                      1: merge vmas for mremap
 *   bit  5 ==    0:                      1: meta-info charge
 *   bit  6 ==    0:                      1: memory lock
 *   bit  7 ==    0:                      1: runtime state consistency check
 *   bit  8 ==    0:                      1: enhanced runtime state consistency check
 *   bit  9 ==    0:                      1: use standard c library malloc, etc.
 *   bit 10 ==    0:                      1: invalid options
 *
 * evict|rsdnt
 *   Determines the state of memory pages when the are allocated. If evict is
 *   selected, memory pages are allocated in the evicted state. If rsdnt is
 *   selected, memory pages are allocated in the resident state. There is a
 *   trade-off between the two states. In the evicted state, a double
 *   segmentation fault is generated when writing to the newly allocated
 *   memory, but SBMA_madmit() is only called for the meta-info if metach is
 *   selected or avoided entirely if nometach is selected. In the resident
 *   state, a double segmentation fault is avoided when writing to the newly
 *   allocated memory, but allocation always requires calling SBMA_madmit().
 *   Default is evict.
 *
 * aggrd|lzyrd
 *   Determines the memory reading strategy to be used by the SBMA runtime. If
 *   aggrd is selected, memory is made resident at the allocation granularity.
 *   This means that when a non-resident page is accessed, all non-resident
 *   pages in the allocation are read from secondary storage simultaneously.
 *   If lzyrd is selected, memory is made resident at the page granularity.
 *   This means that when a non-resident page is accessed, only the page
 *   itself is read from secondary storage. There is a trade-off between the
 *   two memory reading strategies. In the aggrd strategy, reading is more
 *   efficient if the entire allocation will be accessed before being evicted.
 *   In the lzyrd strategy, unnecessary reading is avoided when only subsets
 *   of allocations are accessed between successive evictions. Default is
 *   aggrd.
 *
 * noaggch|aggch
 *   Enables aggressive charging of allocations. This is only valid with lraw
 *   or lrlw. With aggressive allocation charging enabled, instead of charging
 *   each page as it becomes resident, the entire containing allocation is
 *   charged once, when its first page becomes resident. This is a performance
 *   option to decrease the number of calls to SBMA_madmit(). Default is
 *   noaggch.
 *
 * nomerge|merge
 *   Enables the use of an otherwise unnecessary call to mprotect to try to
 *   force the kernel to merge any consecutive vmas before calling mremap in
 *   SBMA_realloc(). This is a performance option intended to increase the
 *   number of times that mremap succeeds, thus reducing the number of times
 *   that SBMA_remap, which is less efficient than SBMA_realloc(), must be
 *   invoked. Default is nomerge.
 *
 * noghost|ghost
 *   Enables the use of a thread-safe memory residency routine. That is, when
 *   memory is being transitioned to the resident state, the vanilla SBMA
 *   implemtation is not thread-safe -- in fact it has a serious
 *   race-condition. When ghost pages are enabled, this race condition is
 *   eliminated by using a temporary buffer to read the data from secondary
 *   storage before atomically remapping the temporary buffer to populate the
 *   application memory. Default is noghost.
 *
 * nometach|metach
 *   Enables charging for the meta-info pages. This means that allocation
 *   table entries and page flags will be charged against the runtime memory
 *   tracking system. With meta-info charging enabled, the resident memory cap
 *   is absolute for the runtime system, instead of a cap only on the amount
 *   of resident application memory. Default is nometach.
 *
 * nomlock|mlock
 *   Enables memory locking in the SBMA runtime. With memory locking enabled,
 *   all memory pages are locked in memory upon becoming resident. This is a
 *   debugging option to ensure that the running processes can fit their
 *   resident memory into the system memory without being swapped by the OS
 *   VMM. Default is nomlock.
 *
 * nocheck|check|extra
 *   Enables runtime state consistency checking. This will check to make sure
 *   that the memory being charged to the system matches the amount accounted
 *   for by the allocation table entries. If extra is specified, then a more
 *   thorough check of the structures will be performed. Default is nocheck.
 *
 * noosvmm|osvmm
 *   Enables the use of the standard C library dynamic memory allocation
 *   functions. When this is enabled, all other options are disabled. Default
 *   is noosvmm.
 *
 * default
 *   evict,lzyrd,noaggch,noghost,merge,nometach,nomlock,nocheck,noosvmm
 */
/****************************************************************************/
enum __sbma_vmm_opt_code
{
  VMM_RSDNT  = 1 << 0,
  VMM_LZYRD  = 1 << 1,
  VMM_AGGCH  = 1 << 2,
  VMM_GHOST  = 1 << 3,
  VMM_MERGE  = 1 << 4,
  VMM_METACH = 1 << 5,
  VMM_MLOCK  = 1 << 6,
  VMM_CHECK  = 1 << 7,
  VMM_EXTRA  = 1 << 8,
  VMM_OSVMM  = 1 << 9,
  VMM_INVLD  = 1 << 10
};


/****************************************************************************/
/*! Struct to return timer values. */
/****************************************************************************/
struct sbma_timeinfo
{
  double tv_rd; /*! read timer */
  double tv_wr; /*! write timer */
  double tv_ad; /*! admit timer */
  double tv_ev; /*! evict timer */
};


/****************************************************************************/
/*!
 * Function prototypes
 */
/****************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

/* klmalloc.c */
int    KL_init(void *, ...);
int    KL_destroy(void);
void * KL_malloc(size_t const);
void * KL_calloc(size_t const, size_t const);
void * KL_realloc(void * const, size_t const);
int    KL_free(void * const);

/* mextra.c */
int                  __sbma_mallopt(int const, int const);
int                  __sbma_parse_optstr(char const * const);
struct mallinfo      __sbma_mallinfo(void);
struct sbma_timeinfo __sbma_timeinfo(void);
int                  __sbma_release(void);

/* mstate.c */
int     __sbma_check(char const * const, int const);
ssize_t __sbma_mtouch(void * const, void * const, size_t const);
ssize_t __sbma_mtouch_atomic(void * const, size_t const, ...);
ssize_t __sbma_mtouchall(void);
ssize_t __sbma_mclear(void * const, size_t const);
ssize_t __sbma_mclearall(void);
ssize_t __sbma_mevict(void * const, size_t const);
ssize_t __sbma_mevictall(void);
int     __sbma_mexist(void const * const);

#ifdef __cplusplus
}
#endif


/* klmalloc.c */
#define SBMA_init(...)          KL_init(NULL, __VA_ARGS__)
#define SBMA_destroy            KL_destroy
#define SBMA_malloc             KL_malloc
#define SBMA_calloc             KL_calloc
#define SBMA_realloc            KL_realloc
#define SBMA_free               KL_free

/* mextra.c */
#define SBMA_mallopt            __sbma_mallopt
#define SBMA_parse_optstr       __sbma_parse_optstr
#define SBMA_mallinfo           __sbma_mallinfo
#define SBMA_release            __sbma_release
#define SBMA_timeinfo           __sbma_timeinfo

/* mstate.c */
#define SBMA_mtouch(...)        __sbma_mtouch(NULL, __VA_ARGS__)
#define SBMA_mtouch_atomic(...) __sbma_mtouch_atomic(__VA_ARGS__, SBMA_ATOMIC_END)
#define SBMA_mtouchall          __sbma_mtouchall
#define SBMA_mclear             __sbma_mclear
#define SBMA_mclearall          __sbma_mclearall
#define SBMA_mevict             __sbma_mevict
#define SBMA_mevictall          __sbma_mevictall
#define SBMA_mexist             __sbma_mexist


#endif
