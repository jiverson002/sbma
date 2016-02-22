/*
Copyright (c) 2015,2016 Jeremy Iverson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/


#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
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


/*****************************************************************************/
/*  MT-Invalid                                                               */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  This function MUST be called EXACTLY ONCE BEFORE any other lock_*  */
/*        function is called.                                                */
/*****************************************************************************/
SBMA_EXTERN int
lock_init(pthread_mutex_t * const lock)
{
  int retval;
  pthread_mutexattr_t attr;

  retval = pthread_mutexattr_init(&attr);
  ERRCHK(RETURN, 0 != retval);

  retval = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  ERRCHK(RETURN, 0 != retval);

  retval = pthread_mutex_init(lock, &attr);
  ERRCHK(RETURN, 0 != retval);

  RETURN:
  return retval;
}
SBMA_EXPORT(internal, int
lock_init(pthread_mutex_t * const lock));


/*****************************************************************************/
/*  MT-Invalid                                                               */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  This function MUST be called EXACTLY ONCE AFTER all other lock_*   */
/*        functions are called.                                              */
/*****************************************************************************/
SBMA_EXTERN int
lock_free(pthread_mutex_t * const lock)
{
  int retval;

  retval = pthread_mutex_destroy(lock);
  ERRCHK(RETURN, 0 != retval);

  RETURN:
  if (0 != retval)
    printf("[%5d] %s\n", (int)getpid(), strerror(retval));
  return retval;
}
SBMA_EXPORT(internal, int
lock_free(pthread_mutex_t * const lock));


/*****************************************************************************/
/*  MT-Safe                                                                  */
/*****************************************************************************/
SBMA_EXTERN int
lock_get_int(char const * const func, int const line,
               char const * const lock_str, pthread_mutex_t * const lock)
{
  int retval;
#if defined(DEADLOCK) && DEADLOCK > 0
  struct timespec ts;

  retval = clock_gettime(CLOCK_REALTIME, &ts);
  ERRCHK(RETURN, -1 == retval);

  ts.tv_sec += 10;

  retval = pthread_mutex_timedlock(lock, &ts);
  ERRCHK(RETURN, 0 != retval && ETIMEDOUT != retval);

  if (ETIMEDOUT == retval) {
    DL_PRINTF("[%5d] Mutex lock timed-out %s:%d %s (%p)\n",\
              (int)syscall(SYS_gettid), func, line, lock_str, (void*)(lock));
#endif

  retval = pthread_mutex_lock(lock);
  ERRCHK(RETURN, 0 != retval);

#if defined(DEADLOCK) && DEADLOCK > 0
  }
#endif

  DL_PRINTF("[%5d] mtx get %s:%d %s (%p)\n", (int)syscall(SYS_gettid), func,\
     line, lock_str, (void*)(lock));

  RETURN:
  return retval;
}
SBMA_EXPORT(internal, int
lock_get_int(char const * const func, int const line,
               char const * const lock_str,
               pthread_mutex_t * const lock));


/*****************************************************************************/
/*  MT-Safe                                                                  */
/*****************************************************************************/
SBMA_EXTERN int
lock_let_int(char const * const func, int const line,
               char const * const lock_str, pthread_mutex_t * const lock)
{
  int retval;

  retval = pthread_mutex_unlock(lock);
  ERRCHK(RETURN, 0 != retval);

  DL_PRINTF("[%5d] mtx let %s:%d %s (%p)\n", (int)syscall(SYS_gettid), func,\
    line, lock_str, (void*)(lock));

  RETURN:
  return retval;
}
SBMA_EXPORT(internal, int
lock_let_int(char const * const func, int const line,
               char const * const lock_str,
               pthread_mutex_t * const lock));
#else
/* Required incase USE_THREAD is not defined, so that this is not an empty
 * translation unit. */
typedef int make_iso_compilers_happy;
#endif
