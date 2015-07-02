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


#include <malloc.h>    /* struct mallinfo */
#include <stddef.h>    /* size_t */
#include <sys/types.h> /* ssize_t */
#include "config.h"
#include "klmalloc.h"
#include "sbma.h"


/****************************************************************************/
/*! Required function prototypes. */
/****************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

/* malloc.c */
void * __sbma_malloc(size_t const __size);
void * __sbma_calloc(size_t const __num, size_t const __size);
void * __sbma_realloc(void * const __ptr, size_t const __size);
int    __sbma_free(void * const __ptr);
int    __sbma_remap(void * const __nptr, void * const __ptr);

/* mcntrl.c */
int __sbma_init(char const * const __fstem, int const __uniq,
                size_t const __page_size, int const __n_procs,
                size_t const __max_mem, int const __opts);
int __sbma_destroy(void);

/* mextra.c */
int             __sbma_mallopt(int const __param, int const __value);
struct mallinfo __sbma_mallinfo(void);

/* mstate.c */
ssize_t __sbma_mtouch(void * const __addr, size_t const __len);
ssize_t __sbma_mtouchall(void);
ssize_t __sbma_mclear(void * const __addr, size_t const __len);
ssize_t __sbma_mclearall(void);
ssize_t __sbma_mevict(void * const __addr, size_t const __len);
ssize_t __sbma_mevictall(void);
int     __sbma_mexist(void const * const __addr);
int     __sbma_eligible(int const __eligible);
int     __sbma_is_eligible(void);

#ifdef __cplusplus
}
#endif


/****************************************************************************/
/*! API creator macro. */
/****************************************************************************/
#define API(__PFX, __RETTYPE, __FUNC, __PPARAMS, __PARAMS)\
  SBMA_EXTERN __RETTYPE SBMA_ ## __FUNC __PPARAMS {\
    return __PFX ## _ ## __FUNC __PARAMS;\
  }\
  SBMA_EXPORT(default, __RETTYPE SBMA_ ## __FUNC __PPARAMS);


/****************************************************************************/
/*! API */
/****************************************************************************/
/* malloc.c */
API(KL,     void *, malloc,  (size_t const a), (a))
API(KL,     void *, calloc,  (size_t const a, size_t const b), (a, b))
API(KL,     void *, realloc, (void * const a, size_t const b), (a, b))
API(KL,     int,    free,    (void * const a), (a))
#if SBMA_VERSION < 200
API(__sbma, int,    remap,   (void * const a, void * const b), (a, b))
#endif

/* mcntrl.c */
SBMA_EXTERN int
SBMA_init(char const * const a, int const b, size_t const c, int const d,
          size_t const e, int const f)
{
  /* init the sbma subsystem */
  if (-1 == __sbma_init(a, b, c, d, e, f))
    return -1;

  /* enable the klmalloc subsystem */
  if (-1 == KL_mallopt(M_ENABLED, M_ENABLED_ON))
    return -1;

  return 0;
}
SBMA_EXTERN int
SBMA_destroy(void)
{
  /* disable the klmalloc subsystem */
  if (-1 == KL_mallopt(M_ENABLED, M_ENABLED_OFF))
    return -1;

  /* destroy the sbma subsystem */
  if (-1 == __sbma_destroy())
    return -1;

  return 0;
}

/* mextra.c */
API(__sbma, int,             mallopt, (int const a, int const b), (a, b))
API(__sbma, struct mallinfo, mallinfo, (void), ())
API(__sbma, int,             eligible, (int const a), (a))
API(__sbma, int,             is_eligible, (void), ())

/* mstate.c */
API(__sbma, ssize_t, mtouch,    (void * const a, size_t const b), (a, b))
API(__sbma, ssize_t, mtouchall, (void), ())
API(__sbma, ssize_t, mclear,    (void * const a, size_t const b), (a, b))
API(__sbma, ssize_t, mclearall, (void), ())
API(__sbma, ssize_t, mevict,    (void * const a, size_t const b), (a, b))
API(__sbma, ssize_t, mevictall, (void), ())
API(__sbma, int,     mexist,    (void const * const a), (a))
