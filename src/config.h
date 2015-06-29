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


#include <stdlib.h>    /* abort */
#include <string.h>    /* basename */
#include <sys/types.h> /* ssize_t */


/* BDMPI requires pthread support */
#ifdef _BDMPI
# ifndef USE_PTHREAD
#   define USE_PTHREAD
# endif
#endif


/****************************************************************************/
/*! Pthread configurations. */
/****************************************************************************/
#ifdef USE_PTHREAD
# include <errno.h>       /* errno library */
# include <pthread.h>     /* pthread library */
# include <semaphore.h>   /* semaphore library */
# include <stdio.h>       /* printf */
# include <string.h>      /* strerror */
# include <sys/syscall.h> /* SYS_gettid */
# include <time.h>        /* CLOCK_REALTIME, struct timespec, clock_gettime */
# include <unistd.h>      /* syscall */

# define DEADLOCK 0   /* 0: no deadlock diagnostics, */
                      /* 1: deadlock diagnostics */

# if defined(DEADLOCK) && DEADLOCK > 0
#   define DL_PRINTF(...) printf(__VA_ARGS__), fflush(stdout)
# else
#   define DL_PRINTF(...) (void)0
# endif

static __thread int retval;
static __thread struct timespec ts;
static __thread pthread_mutexattr_t attr;

# define LOCK_INIT(LOCK)                                                    \
  /* The locks must be of recursive type in-order for the multi-threaded
   * code to work.  This is because a process might receive a SIGIPC while
   * handling a SIGSEGV. */                                                 \
(                                                                           \
  ((0 != (retval=pthread_mutexattr_init(&attr))) ||                         \
   (0 != (retval=pthread_mutexattr_settype(&attr,                           \
    PTHREAD_MUTEX_RECURSIVE))) ||                                           \
   (0 != (retval=pthread_mutex_init(LOCK, &attr))))                         \
    ? (DL_PRINTF("Mutex init failed@%s:%d [retval %d %s]\n", __func__,      \
       __LINE__, retval, strerror(retval)), -1)                             \
    : 0                                                                     \
)
# define LOCK_FREE(LOCK) pthread_mutex_destroy(LOCK)
# define _LOCK_GET(LOCK)                                                    \
(                                                                           \
  (0 != (retval=pthread_mutex_lock(LOCK)))                                  \
    ? (DL_PRINTF("Mutex lock failed@%s:%d [retval: %d %s]\n", __func__,     \
      __LINE__, retval, strerror(retval)), -1)                              \
    : (DL_PRINTF("[%5d] mtx get %s:%d %s (%p)\n", (int)syscall(SYS_gettid), \
        __func__, __LINE__, #LOCK, (void*)(LOCK)), 0)                       \
)
# define LOCK_GET(LOCK)                                                     \
(                                                                           \
  (0 != clock_gettime(CLOCK_REALTIME, &ts))                                 \
    ? (DL_PRINTF("Clock get time failed [errno: %d %s]\n", errno,           \
        strerror(errno)), -1)                                               \
    : (ts.tv_sec += 10,                                                     \
      (0 != (retval=pthread_mutex_timedlock(LOCK, &ts)))                    \
        ? (ETIMEDOUT == retval)                                             \
          ? (DL_PRINTF("[%5d] Mutex lock timed-out %s:%d %s (%p)\n",        \
              (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK,          \
              (void*)(LOCK)), _LOCK_GET(LOCK))                              \
          : (DL_PRINTF("Mutex lock failed [retval: %d %s]\n", retval,       \
              strerror(retval)), -1)                                        \
        : (DL_PRINTF("[%5d] mtx get %s:%d %s (%p)\n",                       \
            (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK,            \
            (void*)(LOCK)), 0))                                             \
)
# define LOCK_LET(LOCK)                                                     \
(                                                                           \
  (0 != (retval=pthread_mutex_unlock(LOCK)))                                \
    ? (DL_PRINTF("Mutex unlock failed@%s:%d [retval: %d %s]\n", __func__,   \
      __LINE__, retval, strerror(retval)), -1)                              \
    : (DL_PRINTF("[%5d] mtx let %s:%d %s (%p)\n", (int)syscall(SYS_gettid), \
        __func__, __LINE__, #LOCK, (void*)(LOCK)), 0)                       \
)
#else
# define LOCK_INIT(LOCK) 0
# define LOCK_FREE(LOCK) 0
# define LOCK_GET(LOCK)  0
# define LOCK_LET(LOCK)  0
#endif


/****************************************************************************/
/*! Assert function. */
/****************************************************************************/
#ifndef NDEBUG
# define ASSERT(COND)                                                       \
do {                                                                        \
  if (0 == (COND)) {                                                        \
    fprintf(stderr, "[%5d] assertion failed: %s:%d: %s\n", (int)getpid(),   \
      basename(__FILE__), __LINE__, #COND);                                 \
    abort();                                                                \
  }                                                                         \
} while (0)
#endif


/****************************************************************************/
/*! Function prototypes for libc hooks. */
/****************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

extern int     libc_open(char const *, int, ...);
extern ssize_t libc_read(int const, void * const, size_t const);
extern ssize_t libc_write(int const, void const * const, size_t const);
extern int     libc_mlock(void const * const, size_t const);
extern int     libc_sem_wait(sem_t * const sem);
extern int     libc_sem_timedwait(sem_t * const sem,
                                  struct timespec const * const ts);

#ifdef __cplusplus
}
#endif


#endif
