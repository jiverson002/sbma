#ifndef __CONFIG_H__
#define __CONFIG_H__ 1


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
# include <stdio.h>       /* printf */
# include <string.h>      /* strerror */
# include <sys/syscall.h> /* SYS_gettid */
# include <time.h>        /* CLOCK_REALTIME, struct timespec, clock_gettime */
# include <unistd.h>      /* syscall */

# define DEADLOCK 0   /* 0: no deadlock diagnostics, */
                      /* 1: deadlock diagnostics */

# if defined(DEADLOCK) && DEADLOCK > 0
#   define DL_PRINTF(...) printf(__VA_ARGS__)
# else
#   define DL_PRINTF(...) (void)0
# endif

static __thread int retval;
static __thread struct timespec ts;

# define LOCK_INIT(LOCK) pthread_mutex_init(LOCK, NULL)
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
/*! Function prototypes for libc hooks. */
/****************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

extern int     libc_open(char const *, int, ...);
extern ssize_t libc_read(int const, void * const, size_t const);
extern ssize_t libc_write(int const, void const * const, size_t const);
extern int     libc_mlock(void const * const, size_t const);

#ifdef __cplusplus
}
#endif


#endif
