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

#ifndef __CONFIG_H__
#define __CONFIG_H__ 1


#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif


#include <semaphore.h>  /* semaphore libray */
#include <stdio.h>      /* fprintf */
#include <stdlib.h>     /* abort, stderr */
#include <string.h>     /* basename */
#include <sys/types.h>  /* ssize_t */
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
/*! Assert function. */
/****************************************************************************/
//#ifndef NDEBUG
# define ASSERT(COND)                                                       \
do {                                                                        \
  if (0 == (COND)) {                                                        \
    fprintf(stderr, "[%5d] assertion failed: %s:%d: %s\n", (int)getpid(),   \
      basename(__FILE__), __LINE__, #COND);                                 \
    abort();                                                                \
  }                                                                         \
} while (0)
//#else
//# define ASSERT(COND) (void)0
//#endif


/****************************************************************************/
/*! Process deadlock check functions. */
/****************************************************************************/
//#define DEADLOCK_ALARM_TIME  10000
#ifdef DEADLOCK_ALARM_TIME
# include <time.h>    /* CLOCK_REALTIME, struct timespec, clock_gettime */
# define DEADLOCK_ALARM_ON()\
do {\
  /*printf("[%5d] %s:%d alarm on\n", (int)getpid(), basename(__FILE__),\
    __LINE__);*/\
  alarm(DEADLOCK_ALARM_TIME);\
} while (0)
# define DEADLOCK_ALARM_OFF()\
do {\
  /*printf("[%5d] %s:%d alarm off\n", (int)getpid(), basename(__FILE__),\
    __LINE__);*/\
  alarm(0);\
} while (0)
#else
# define DEADLOCK_ALARM_ON()  (void)0
# define DEADLOCK_ALARM_OFF() (void)0
#endif


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
int     libc_sem_wait(sem_t * const);
int     libc_sem_timedwait(sem_t * const, struct timespec const * const);

#ifdef __cplusplus
}
#endif


#endif