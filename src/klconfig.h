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

#ifndef __KLCONFIG_H__
#define __KLCONFIG_H__ 1


/* enable use of assert */
#ifdef NDEBUG
# undef NDEBUG
#endif


/* BDMPI requires pthread support */
#ifdef _BDMPI
# ifndef USE_PTHREAD
#   define USE_PTHREAD
# endif
#endif


/*--------------------------------------------------------------------------*/


#ifdef USE_PTHREAD
# define DEADLOCK 0   /* 0: no deadlock diagnostics, */
                      /* 1: deadlock diagnostics */

  /* Access to PTHREAD_MUTEX_RECURSIVE and SYS_gettid */
# ifndef _GNU_SOURCE
#   define _GNU_SOURCE
# endif
# include <malloc.h>      /* struct mallinfo */
# include <pthread.h>     /* pthread library */
# include <sys/syscall.h> /* SYS_gettid */
# include <unistd.h>      /* syscall */
# undef _GNU_SOURCE
  /* Access to clock_gettime and related */
# ifndef _POSIX_C_SOURCE
#   define _POSIX_C_SOURCE 200112L
# else
#   if _POSIX_C_SOURCE < 200112L
#     undef _POSIX_C_SOURCE
#     define _POSIX_C_SOURCE 200112L
#   endif
# endif
# include <time.h>    /* CLOCK_REALTIME, struct timespec, clock_gettime */

# include <errno.h>   /* errno library */
# include <pthread.h> /* pthread library */

# define INIT_LOCK(LOCK)                                                    \
do {                                                                        \
  /* The locks must be of recursive type in-order for the multi-threaded
   * code to work.  This is because OpenMP and pthread both call
   * malloc/free internally when spawning/joining threads. */               \
  int retval;                                                               \
  pthread_mutexattr_t attr;                                                 \
  if (0 != (retval=pthread_mutexattr_init(&(attr))))                        \
    goto INIT_LOCK_CLEANUP;                                                 \
  if (0 != (retval=pthread_mutexattr_settype(&(attr),                       \
    PTHREAD_MUTEX_RECURSIVE)))                                              \
    goto INIT_LOCK_CLEANUP;                                                 \
  if (0 != (retval=pthread_mutex_init(LOCK, &(attr))))                      \
    goto INIT_LOCK_CLEANUP;                                                 \
                                                                            \
  goto INIT_LOCK_DONE;                                                      \
                                                                            \
  INIT_LOCK_CLEANUP:                                                        \
  printf("Mutex init failed@%s:%d [retval: %d %s]\n", __func__, __LINE__,   \
    retval, strerror(retval));                                              \
                                                                            \
  INIT_LOCK_DONE: ;                                                         \
} while (0)

# define FREE_LOCK(LOCK)                                                    \
do {                                                                        \
  int retval;                                                               \
  if (0 != (retval=pthread_mutex_destroy(LOCK))) {                          \
    printf("Mutex free failed@%s:%d [retval: %d %s]\n", __func__, __LINE__, \
      retval, strerror(retval));                                            \
  }                                                                         \
} while (0)

# if !defined(DEADLOACK) || DEADLOCK == 0
#   define GET_LOCK(LOCK)                                                   \
do {                                                                        \
  int retval;                                                               \
  if (0 != (retval=pthread_mutex_lock(LOCK))) {                             \
    printf("Mutex lock failed@%s:%d [retval: %d %s]\n", __func__, __LINE__, \
      retval, strerror(retval));                                            \
  }                                                                         \
  else if (DEADLOCK) printf("[%5d] mtx get %s:%d %s (%p)\n",                \
    (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK, (void*)(LOCK));    \
} while (0)
# else
#   define GET_LOCK(LOCK)                                                   \
do {                                                                        \
  int retval;                                                               \
  struct timespec ts;                                                       \
  if (0 != clock_gettime(CLOCK_REALTIME, &ts)) {                            \
    printf("Clock get time failed [errno: %d %s]\n", errno,                 \
      strerror(errno));                                                     \
  }                                                                         \
  ts.tv_sec += 10;                                                          \
  if (0 != (retval=pthread_mutex_timedlock(LOCK, &ts))) {                   \
    if (ETIMEDOUT == retval) {                                              \
      printf("[%5d] Mutex lock timed-out %s:%d %s (%p)\n",                  \
        (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK, (void*)(LOCK));\
      _GET_LOCK(LOCK);                                                      \
    }                                                                       \
    else {                                                                  \
      printf("Mutex lock failed [retval: %d %s]\n", retval,                 \
        strerror(retval));                                                  \
    }                                                                       \
  }                                                                         \
  if (DEADLOCK) printf("[%5d] mtx get %s:%d %s (%p)\n",                     \
    (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK, (void*)(LOCK));    \
} while (0)
# endif

# define LET_LOCK(LOCK)                                                     \
do {                                                                        \
  int retval;                                                               \
  if (0 != (retval=pthread_mutex_unlock(LOCK))) {                           \
    printf("Mutex unlock failed@%s:%d [retval: %d %s]\n", __func__,         \
      __LINE__, retval, strerror(retval));                                  \
  }                                                                         \
  if (DEADLOCK) printf("[%5d] mtx let %s:%d %s (%p)\n",                     \
    (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK, (void*)(LOCK));    \
} while (0)
#else
# define INIT_LOCK(LOCK)
# define FREE_LOCK(LOCK)
# define GET_LOCK(LOCK)
# define LET_LOCK(LOCK)
#endif


/*--------------------------------------------------------------------------*/


#ifdef USE_OPENMP
# include <omp.h>
#else
# define omp_get_thread_num()   0
# define omp_get_num_threads()  1
#endif


/*--------------------------------------------------------------------------*/


//#define USE_MMAP
#if defined(USE_MMAP) && defined(USE_MEMALIGN)
# undef USE_MMAP
#endif
#if defined(USE_MMAP) && defined(USE_SBMALLOC)
# undef USE_SBMALLOC
#endif
#if defined(USE_MEMALIGN) && defined(USE_SBMALLOC)
# undef USE_SBMALLOC
#endif
#if !defined(USE_MMAP) && !defined(USE_MEMALIGN) && !defined(USE_SBMALLOC)
# define USE_SBMALLOC
#endif


#ifdef USE_MMAP
# ifndef _BSD_SOURCE
#   define _BSD_SOURCE
# endif
# include <sys/mman.h> /* mmap, munmap */
# undef _BSD_SOURCE
# define SYS_ALLOC_FAIL MAP_FAILED
# define CALL_SYS_ALLOC(P,S) \
  ((P)=mmap(NULL, S, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0))
# define CALL_SYS_FREE(P,S)  munmap(P,S)
# define CALL_SYS_BZERO(P,S)
#endif
#ifdef USE_MEMALIGN
# ifndef _POSIX_C_SOURCE
#   define _POSIX_C_SOURCE 200112L
# elif _POSIX_C_SOURCE < 200112L
#   undef _POSIX_C_SOURCE
#   define _POSIX_C_SOURCE 200112L
# endif
# include <stdlib.h>  /* posix_memalign */
# undef _POSIX_C_SOURCE
# define SYS_ALLOC_FAIL NULL
# define CALL_SYS_ALLOC(P,S) \
  (0 == posix_memalign(&(P),MEMORY_ALLOCATION_ALIGNMENT,S) ? (P) : NULL)
# define CALL_SYS_FREE(P,S)  libc_free(P)
# define CALL_SYS_BZERO(P,S) memset(P, 0, S)
#endif
#ifdef USE_SBMALLOC
void * sbma_malloc(size_t const size);
void * sbma_realloc(void * const ptr, size_t const size);
int sbma_free(void * const ptr);
int sbma_remap(void * const nptr, void * const ptr, size_t const num,
               size_t const off);
# define SYS_ALLOC_FAIL NULL
# define CALL_SYS_ALLOC(P,S)       ((P)=sbma_malloc(S))
# define CALL_SYS_REALLOC(N,O,S,F) ((N)=sbma_realloc(O,F))
# define CALL_SYS_REMAP(N,O,S,F)   sbma_remap(N,O,S,F)
# define CALL_SYS_FREE(P,S)        sbma_free(P)
# define CALL_SYS_BZERO(P,S)
#endif


#endif
