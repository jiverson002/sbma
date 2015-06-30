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


#define SBMA_MAJOR 0
#define SBMA_MINOR 1
#define SBMA_PATCH 5
#define SBMA_RCAND 0


#define SBMA_DEFAULT_PAGE_SIZE (1<<14)
#define SBMA_DEFAULT_FSTEM     "/tmp/"
#define SBMA_DEFAULT_OPTS      0


#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************/
/*! Mallopt parameters. */
/****************************************************************************/
enum SBMA_mallopt_params
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
enum SBMA_vmm_opt_code
{
  VMM_LZYRD = 1 << 0,
  VMM_LZYWR = 1 << 1,
  VMM_GHOST = 1 << 2
};


/* malloc.c */
void * SBMA_malloc(size_t const size);
void * SBMA_calloc(size_t const num, size_t const size);
int    SBMA_free(void * const ptr);
void * SBMA_realloc(void * const ptr, size_t const size);

/* mcntrl.c */
int SBMA_init(char const * const fstem, size_t const page_size,
              int const n_procs, size_t const max_mem, int const opts);
int SBMA_destroy(void);

/* mextra.c */
int             SBMA_mallopt(int const param, int const value);
struct mallinfo SBMA_mallinfo(void);

/* mstate.c */
ssize_t SBMA_mtouch(void * const ptr, size_t const size);
ssize_t SBMA_mtouchall(void);
ssize_t SBMA_mclear(void * const ptr, size_t const size);
ssize_t SBMA_mclearall(void);
ssize_t SBMA_mevict(void * const ptr, size_t const size);
ssize_t SBMA_mevictall(void);
int     SBMA_mexist(void const * const ptr);

#ifdef __cplusplus
}
#endif


#endif
