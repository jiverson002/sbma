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


#include <stdarg.h> /* stdarg library */
#include <stddef.h> /* size_t */
#include "config.h"
#include "lock.h"
#include "vmm.h"


/****************************************************************************/
/*! Initialization variables. */
/****************************************************************************/
#ifdef USE_THREAD
static pthread_mutex_t init_lock=PTHREAD_MUTEX_INITIALIZER;
#endif


/****************************************************************************/
/*! The single instance of vmm per process. */
/****************************************************************************/
struct vmm vmm={.init=0, .ipc.init=0};


/****************************************************************************/
/*! Initialize the sbma environment. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_init(char const * const __fstem, int const __uniq,
            size_t const __page_size, int const __n_procs,
            size_t const __max_mem, int const __opts)
{
  /* acquire init lock */
  if (-1 == __lock_get(&init_lock))
    return -1;

  if (-1 == __vmm_init(&vmm, __fstem, __uniq, __page_size, __n_procs,\
      __max_mem, __opts))
  {
    (void)__lock_let(&init_lock);
    return -1;
  }

  /* release init lock */
  if (-1 == __lock_let(&init_lock))
    return -1;

  return 0;
}
SBMA_EXPORT(internal, int
__sbma_init(char const * const __fstem, int const __uniq,
            size_t const __page_size, int const __n_procs,
            size_t const __max_mem, int const __opts));


/****************************************************************************/
/*! Initialize the sbma environment from a va_list. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_vinit(va_list args)
{
  char const * fstem     = va_arg(args, char const *);
  int const uniq         = va_arg(args, int);
  size_t const page_size = va_arg(args, size_t);
  int const n_procs      = va_arg(args, int);
  size_t const max_mem   = va_arg(args, size_t);
  int const opts         = va_arg(args, int);
  return __sbma_init(fstem, uniq, page_size, n_procs, max_mem, opts);
}
SBMA_EXPORT(internal, int
__sbma_vinit(va_list args));


/****************************************************************************/
/*! Destroy the sbma environment. */
/****************************************************************************/
SBMA_EXTERN int
__sbma_destroy(void)
{
  /* acquire init lock */
  if (-1 == __lock_get(&init_lock))
    return -1;

  if (-1 == __vmm_destroy(&vmm)) {
    (void)__lock_let(&init_lock);
    return -1;
  }

  /* release init lock */
  if (-1 == __lock_let(&init_lock))
    return -1;

  return 0;
}
SBMA_EXPORT(internal, int
__sbma_destroy(void));
