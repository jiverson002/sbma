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


/****************************************************************************/
/*! Pthread configurations. */
/****************************************************************************/
#ifdef USE_THREAD
# include <errno.h>       /* errno library */
# include <pthread.h>     /* pthread library */
# include <stdio.h>       /* printf */
# include <string.h>      /* strerror */
# include <sys/syscall.h> /* SYS_gettid */
# include <time.h>        /* CLOCK_REALTIME, struct timespec, clock_gettime */
# include <unistd.h>      /* syscall */
# include "common.h"
# include "lock.h"

# if defined(DEADLOCK) && DEADLOCK > 0
#   define DL_PRINTF(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
# else
#   define DL_PRINTF(...) (void)0
# endif

SBMA_EXTERN int
__lock_init(pthread_mutex_t * const __lock)
{
  int ret;
  pthread_mutexattr_t attr;

  ret = pthread_mutexattr_init(&attr);
  if (0 != ret)
    return ret;
  ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  if (0 != ret)
    return ret;
  ret = pthread_mutex_init(__lock, &attr);
  if (0 != ret)
    return ret;

  return 0;
}
SBMA_EXPORT(internal, int
__lock_init(pthread_mutex_t * const __lock));


SBMA_EXTERN int
__lock_free(pthread_mutex_t * const __lock)
{
  int ret;

  ret = pthread_mutex_destroy(__lock);
  if (0 != ret)
    return ret;

  return 0;
}
SBMA_EXPORT(internal, int
__lock_free(pthread_mutex_t * const __lock));


SBMA_EXTERN int
__lock_get_int(char const * const __func, int const __line,
               char const * const __lock_str, pthread_mutex_t * const __lock)
{
  int ret;
#if defined(DEADLOCK) && DEADLOCK > 0
  struct timespec ts;

  ret = clock_gettime(CLOCK_REALTIME, &ts);
  if (-1 == ret)
    return -1;
  ts.tv_sec += 10;

  ret = pthread_mutex_timedlock(__lock, &ts);
  if (ETIMEDOUT == ret) {
    DL_PRINTF("[%5d] Mutex lock timed-out %s:%d %s (%p)\n",\
              (int)syscall(SYS_gettid), __func, __line, __lock_str,\
              (void*)(__lock));
#endif
    ret = pthread_mutex_lock(__lock);
    if (0 != ret)
      return ret;
#if defined(DEADLOCK) && DEADLOCK > 0
  }
  else if (0 != ret) {
    return ret;
  }
#endif

  DL_PRINTF("[%5d] mtx get %s:%d %s (%p)\n", (int)syscall(SYS_gettid),\
    __func, __line, __lock_str, (void*)(__lock));

  return 0;
}
SBMA_EXPORT(internal, int
__lock_get_int(char const * const __func, int const __line,
               char const * const __lock_str,
               pthread_mutex_t * const __lock));


SBMA_EXTERN int
__lock_let_int(char const * const __func, int const __line,
               char const * const __lock_str, pthread_mutex_t * const __lock)
{
  int ret;

  ret = pthread_mutex_unlock(__lock);
  if (0 != ret)
    return ret;

  DL_PRINTF("[%5d] mtx let %s:%d %s (%p)\n", (int)syscall(SYS_gettid),\
    __func, __line, __lock_str, (void*)(__lock));

  return 0;
}
SBMA_EXPORT(internal, int
__lock_let_int(char const * const __func, int const __line,
               char const * const __lock_str,
               pthread_mutex_t * const __lock));
#else
/* Required incase USE_THREAD is not defined, so that this is not an empty
 * translation unit. */
typedef int make_iso_compilers_happy;
#endif
