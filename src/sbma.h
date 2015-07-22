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
#define SBMA_PATCH   4
#define SBMA_RCAND   -pre
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
 *   bit 0 ==    0: aggressive read  1: lazy read
 *   bit 1 ==    0: aggressive write 1: lazy write
 *   bit 2 ==    0:                  1: ghost pages
 */
/****************************************************************************/
enum __sbma_vmm_opt_code
{
  VMM_OSVMM = 1 << 0,
  VMM_LZYRD = 1 << 1,
  VMM_LZYWR = 1 << 2,
  VMM_GHOST = 1 << 3
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
int    KL_init(void * unused, ...);
int    KL_destroy(void);
void * KL_malloc(size_t const size);
void * KL_calloc(size_t const num, size_t const size);
void * KL_realloc(void * const ptr, size_t const size);
int    KL_free(void * const ptr);

/* mextra.c */
int             __sbma_mallopt(int const param, int const value);
struct mallinfo __sbma_mallinfo(void);
int             __sbma_release(void);

/* mstate.c */
ssize_t __sbma_mtouch(void * const ptr, size_t const size);
ssize_t __sbma_mtouch_atomic(void * const ptr, size_t const size, ...);
ssize_t __sbma_mtouchall(void);
ssize_t __sbma_mclear(void * const ptr, size_t const size);
ssize_t __sbma_mclearall(void);
ssize_t __sbma_mevict(void * const ptr, size_t const size);
ssize_t __sbma_mevictall(void);
int     __sbma_mexist(void const * const ptr);

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
#define SBMA_mallinfo           __sbma_mallinfo
#define SBMA_release            __sbma_release

/* mstate.c */
#define SBMA_mtouch             __sbma_mtouch
#define SBMA_mtouch_atomic(...) __sbma_mtouch_atomic(__VA_ARGS__, SBMA_ATOMIC_END)
#define SBMA_mtouchall          __sbma_mtouchall
#define SBMA_mclear             __sbma_mclear
#define SBMA_mclearall          __sbma_mclearall
#define SBMA_mevict             __sbma_mevict
#define SBMA_mevictall          __sbma_mevictall
#define SBMA_mexist             __sbma_mexist


#endif
