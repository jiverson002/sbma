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


#ifdef NDEBUG
# undef NDEBUG
#endif


#include <fcntl.h>     /* O_RDWR, O_CREAT, O_EXCL */
#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t */
#include <stdio.h>     /* FILENAME_MAX */
#include <string.h>    /* memcpy */
#include <sys/mman.h>  /* mmap, mremap, munmap, madvise, mprotect */
#include <sys/stat.h>  /* S_IRUSR, S_IWUSR */
#include <sys/types.h> /* truncate */
#include <unistd.h>    /* truncate */
#include "config.h"
#include "vmm.h"

#include "klmalloc.h"


/****************************************************************************/
/*! Initialization variables. */
/****************************************************************************/
static int init=0;
#ifdef USE_PTHREAD
static pthread_mutex_t init_lock=PTHREAD_MUTEX_INITIALIZER;
#endif


/****************************************************************************/
/*! The single instance of vmm per process. */
/****************************************************************************/
struct vmm vmm;


/****************************************************************************/
/*! Initialize the ooc environment. */
/****************************************************************************/
extern int
__ooc_init__(char const * const __fstem, size_t const __page_size,
             int const __n_procs, size_t const __max_mem, int const __opts)
{
  /* acquire init lock */
  if (-1 == LOCK_GET(&init_lock))
    return -1;

  /* check if init and init if necessary */
  if (0 == init) {
    if (-1 == __vmm_init__(&vmm, __page_size, __fstem, __n_procs, __max_mem,
        __opts))
    {
      (void)LOCK_LET(&init_lock);
      return -1;
    }

    init = 1;
  }

  /* release init lock */
  if (-1 == LOCK_LET(&init_lock))
    return -1;

  return 0;
}


/****************************************************************************/
/*! Destroy the ooc environment. */
/****************************************************************************/
extern int
__ooc_destroy__(void)
{
  /* acquire init lock */
  if (-1 == LOCK_GET(&init_lock))
    return -1;

  /* check if init and destroy if necessary */
  if (1 == init) {
    if (-1 == __vmm_destroy__(&vmm)) {
      (void)LOCK_LET(&init_lock);
      return -1;
    }

    init = 0;
  }

  /* release init lock */
  if (-1 == LOCK_LET(&init_lock))
    return -1;

  return 0;
}
