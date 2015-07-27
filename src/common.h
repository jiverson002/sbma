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

#ifndef __COMMON_H__
#define __COMMON_H__ 1


#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif


#include <semaphore.h>  /* semaphore libray */
#include <stdio.h>      /* fprintf */
#include <stdlib.h>     /* abort, stderr */
#include <string.h>     /* basename */
#include <sys/types.h>  /* ssize_t */
#include <time.h>       /* struct timespec, nanosleep */
#include <unistd.h>     /* getpid */


/****************************************************************************/
/*! Function attributes. */
/****************************************************************************/
/* If we're not using GNU C, omit __attribute__ */
#ifndef __GNUC__
# define  __attribute__(x)
#endif

#define SBMA_EXTERN extern
#define SBMA_STATIC static
#define SBMA_EXPORT(__VISIBILITY, __DECL)\
  SBMA_EXTERN __DECL __attribute__((__visibility__(#__VISIBILITY)))


/****************************************************************************/
/*! Compile-time options. */
/****************************************************************************/
#define SBMA_RESIDENT_DEFAULT 0
#define SBMA_MERGE_VMA        1
#define SBMA_FILE_RESERVE     0

//#define SBMA_MMAP_FLAG (MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_LOCKED)
#define SBMA_MMAP_FLAG (MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE)


/****************************************************************************/
/*! Assert function. */
/****************************************************************************/
#define ASSERT(COND)                                                        \
do {                                                                        \
  if (0 == (COND)) {                                                        \
    fprintf(stderr, "[%5d] assertion failed: %s:%d: %s\n", (int)getpid(),   \
      basename(__FILE__), __LINE__, #COND);                                 \
    abort();                                                                \
  }                                                                         \
} while (0)


/****************************************************************************/
/*! Wall timer functions. */
/****************************************************************************/
#define TIMER_START(__TS)\
do {\
  struct timespec __TIMER_ts;\
  if (-1 == clock_gettime(CLOCK_MONOTONIC, &__TIMER_ts))\
    ASSERT(0);\
  (__TS)->tv_sec  = __TIMER_ts.tv_sec;\
  (__TS)->tv_nsec = __TIMER_ts.tv_nsec;\
} while (0)

#define TIMER_STOP(__TS)\
do {\
  struct timespec __TIMER_ts;\
  if (-1 == clock_gettime(CLOCK_MONOTONIC, &__TIMER_ts))\
    ASSERT(0);\
  if ((__TS)->tv_sec == __TIMER_ts.tv_sec) {\
    (__TS)->tv_sec  = __TIMER_ts.tv_sec-(__TS)->tv_sec;\
    (__TS)->tv_nsec = __TIMER_ts.tv_nsec-(__TS)->tv_nsec;\
  }\
  else {\
    (__TS)->tv_sec  = __TIMER_ts.tv_sec-(__TS)->tv_sec-1;\
    (__TS)->tv_nsec = __TIMER_ts.tv_nsec+1000000000l-(__TS)->tv_nsec;\
  }\
} while (0)


/****************************************************************************/
/*! Function prototypes for libc hooks. */
/****************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

void *  libc_memcpy(void * const, void const * const, size_t const);
void *  libc_memmove(void * const, void const * const, size_t const);
int     libc_open(char const *, int, ...);
ssize_t libc_read(int const, void * const, size_t const);
ssize_t libc_write(int const, void const * const, size_t const);
int     libc_mlock(void const * const, size_t const);
int     libc_msync(void * const, size_t const, int const);
int     libc_nanosleep(struct timespec const * const, struct timespec * const);
int     libc_sem_wait(sem_t * const);

#ifdef __cplusplus
}
#endif

#endif
