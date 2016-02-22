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


#ifndef SBMA_COMMON_H
#define SBMA_COMMON_H 1


#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif


#include <semaphore.h>  /* semaphore libray */
#include <stdio.h>      /* fprintf */
#include <stdlib.h>     /* abort, stderr */
#include <string.h>     /* basename */
#include <sys/stat.h>   /* stat, open */
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
#define SBMA_FILE_RESERVE 0

#define SBMA_MMAP_FLAG\
  (MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|\
    (VMM_MLOCK == (_vmm_.opts&VMM_MLOCK) ? MAP_LOCKED : 0))


/****************************************************************************/
/*! Check the consistency of the runtime state. */
/****************************************************************************/
# define SBMA_STATE_CHECK()\
do {\
  int _ret = sbma_check(__func__, __LINE__);\
  ASSERT(-1 != _ret);\
} while (0)


/****************************************************************************/
/*! Assert function. */
/****************************************************************************/
#define ASSERT(COND)\
do {\
  if (0 == (COND)) {\
    fprintf(stderr, "[%5d] assertion failed: %s:%d: %s\n", (int)getpid(),\
      basename(__FILE__), __LINE__, #COND);\
    abort();\
  }\
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
/*! Check for error and go to predefined label if encountered. */
/****************************************************************************/
#define ERRCHK_DEBUG 1
#define ERRCHK(__LABEL, __COND)\
do {\
  if (__COND) {\
    if (1 == ERRCHK_DEBUG)\
      printf("[%5d] %s:%d\n", (int)getpid(), __func__, __LINE__);\
    goto __LABEL;\
  }\
} while (0)


/****************************************************************************/
/*! Print a fatal error message and abort. */
/****************************************************************************/
#define FATAL_ABORT(__CODE)\
do {\
  fprintf(stderr, "[%5d] An unrecoverable error has occured in %s(), "\
    "possibly caused by `%s'. The runtime state cannont be reverted to its "\
    "previous state. Now aborting...\n", (int)getpid(), __func__,\
    strerror(__CODE));\
  abort();\
} while (0)


/****************************************************************************/
/*! Function prototypes for libc hooks. */
/****************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif


SBMA_EXPORT(internal, void *
  libc_memcpy(void * const dst, void const * const src, size_t const num));
SBMA_EXPORT(internal, void *
  libc_memmove(void * const dst, void const * const src, size_t const num));
SBMA_EXPORT(internal, int libc_stat(char const * path, struct stat * buf));
SBMA_EXPORT(internal, int
  libc___xstat(int ver, char const * path, struct stat * buf));
SBMA_EXPORT(internal, int libc_open(char const * path, int flags, ...));
SBMA_EXPORT(internal, ssize_t
  libc_read(int const fd, void * const buf, size_t const count));
SBMA_EXPORT(internal, ssize_t
  libc_write(int const fd, void const * const buf, size_t const count));
SBMA_EXPORT(internal, size_t
  libc_fread(void * const buf, size_t const size, size_t const num,
             FILE * const stream));
SBMA_EXPORT(internal, size_t
  libc_fwrite(void const * const buf, size_t const size, size_t const num,
              FILE * const stream));
SBMA_EXPORT(internal, int
  libc_mlock(void const * const addr, size_t const len));
SBMA_EXPORT(internal, int libc_mlockall(int flags));
SBMA_EXPORT(internal, int
  libc_msync(void * const addr, size_t const len, int const flags));


#ifdef __cplusplus
}
#endif


#endif /* SBMA_COMMON_H */
