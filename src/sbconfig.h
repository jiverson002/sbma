#ifndef __SBCONFIG_H__
#define __SBCONFIG_H__ 1


#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200112L
#else
# if _POSIX_C_SOURCE < 200112L
#   undef _POSIX_C_SOURCE
#   define _POSIX_C_SOURCE 200112L
# endif
#endif

#ifdef _BDMPI
# ifndef USE_PTHREAD
#   define USE_PTHREAD
# endif
#endif


/*--------------------------------------------------------------------------*/


//#define USE_CHECKSUM


/*--------------------------------------------------------------------------*/


#ifdef USE_OPENMP
# include <omp.h>
#else
# define omp_get_thread_num()   0
# define omp_get_num_threads()  1
#endif


/*--------------------------------------------------------------------------*/


#ifdef USE_PTHREAD
# define SBDEADLOCK 0   /* 0: no deadlock diagnostics, */
                        /* 1: deadlock diagnostics */

# include <errno.h>     /* errno library */
# include <pthread.h>   /* pthread library */
# include <syscall.h>   /* SYS_gettid, syscall */
# include <time.h>      /* CLOCK_REALTIME, struct timespec, clock_gettime */

# define _SB_GET_LOCK(LOCK)                                                 \
do {                                                                        \
  int retval;                                                               \
  if (0 != (retval=pthread_mutex_lock(LOCK))) {                             \
    SBWARN(SBDBG_FATAL)("Mutex lock failed [retval: %d %s]\n", retval,      \
      strerror(retval));                                                    \
    sb_abort(0);                                                            \
  }                                                                         \
  if (SBDEADLOCK) printf("[%5d] mtx get %s:%d %s (%p)\n",                   \
    (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK, (void*)(LOCK));    \
} while (0)

# define SB_INIT_LOCK(LOCK)                                                 \
do {                                                                        \
  /* The locks must be of recursive type in-order for the multi-threaded
   * code to work.  This is because OpenMP and pthread both call
   * malloc/free internally when spawning/joining threads. */               \
  int retval;                                                               \
  pthread_mutexattr_t attr;                                                 \
  if (0 != (retval=pthread_mutexattr_init(&(attr))))                        \
    goto SB_INIT_LOCK_CLEANUP;                                              \
  if (0 != (retval=pthread_mutexattr_settype(&(attr),                       \
    PTHREAD_MUTEX_RECURSIVE)))                                              \
    goto SB_INIT_LOCK_CLEANUP;                                              \
  if (0 != (retval=pthread_mutex_init(LOCK, &(attr))))                      \
    goto SB_INIT_LOCK_CLEANUP;                                              \
                                                                            \
  goto SB_INIT_LOCK_DONE;                                                   \
                                                                            \
  SB_INIT_LOCK_CLEANUP:                                                     \
  SBWARN(SBDBG_FATAL)("Mutex init failed [retval: %d %s]\n", retval,        \
    strerror(retval));                                                      \
  sb_abort(0);                                                              \
                                                                            \
  SB_INIT_LOCK_DONE: ;                                                      \
} while (0)

# define SB_FREE_LOCK(LOCK)                                                 \
do {                                                                        \
  int retval;                                                               \
  if (0 != (retval=pthread_mutex_destroy(LOCK))) {                          \
    SBWARN(SBDBG_FATAL)("Mutex destroy failed [retval: %d %s]\n", retval,   \
      strerror(retval));                                                    \
    sb_abort(0);                                                            \
  }                                                                         \
} while (0)

# if !defined(SBDEADLOACK) || SBDEADLOCK == 0
#   define SB_GET_LOCK(LOCK) _SB_GET_LOCK(LOCK)
# else
#   define SB_GET_LOCK(LOCK)                                                \
do {                                                                        \
  int retval;                                                               \
  struct timespec ts;                                                       \
  if (0 != clock_gettime(CLOCK_REALTIME, &ts)) {                            \
    SBWARN(SBDBG_FATAL)("Clock get time failed [errno: %d %s]\n", errno,    \
      strerror(errno));                                                     \
    sb_abort(0);                                                            \
  }                                                                         \
  ts.tv_sec += 10;                                                          \
  if (0 != (retval=pthread_mutex_timedlock(LOCK, &ts))) {                   \
    if (ETIMEDOUT == retval) {                                              \
      SBWARN(SBDBG_DIAG)("[%5d] mtx lock timed-out %s:%d %s (%p)\n",        \
        (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK, (void*)(LOCK));\
      _SB_GET_LOCK(LOCK);                                                   \
    }                                                                       \
    else {                                                                  \
      SBWARN(SBDBG_FATAL)("Mutex lock failed [retval: %d %s]\n", retval,    \
        strerror(retval));                                                  \
      sb_abort(0);                                                          \
    }                                                                       \
  }                                                                         \
  if (SBDEADLOCK) printf("[%5d] mtx get %s:%d %s (%p)\n",                   \
    (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK, (void*)(LOCK));    \
} while (0)
# endif

# define SB_LET_LOCK(LOCK)                                                  \
do {                                                                        \
  int retval;                                                               \
  if (0 != (retval=pthread_mutex_unlock(LOCK))) {                           \
    SBWARN(SBDBG_FATAL)("Mutex unlock failed [retval: %d %s]\n", retval,    \
      strerror(retval));                                                    \
    sb_abort(0);                                                            \
  }                                                                         \
  if (SBDEADLOCK) printf("[%5d] mtx let %s:%d %s (%p)\n",                   \
    (int)syscall(SYS_gettid), __func__, __LINE__, #LOCK, (void*)(LOCK));    \
} while (0)
#else
# define SB_INIT_LOCK(LOCK)
# define SB_FREE_LOCK(LOCK)
# define SB_GET_LOCK(LOCK)
# define SB_LET_LOCK(LOCK)
#endif


/*--------------------------------------------------------------------------*/


#define SBPRINT2(...)                                                       \
  fprintf(stderr, __VA_ARGS__);                                             \
  fprintf(stderr, "\n");                                                    \
  fflush(stderr);                                                           \
} while (0)

#define SBPRINT1(LEVEL)                                                     \
do {                                                                        \
  fprintf(stderr, "sb: %s: ", sb_dbg_str[LEVEL]);                           \
  SBPRINT2

#define SBWARN(LEVEL) \
  if (LEVEL <= sb_opts[SBOPT_DEBUG]) SBPRINT1(LEVEL)


/*--------------------------------------------------------------------------*/


#ifndef NDEBUG
# define sb_assert(EXPR)                                                    \
do {                                                                        \
  if (!(EXPR)) {                                                            \
    fprintf(stderr, "ASSERTION FAILED: %d: %s\n", __LINE__, #EXPR);         \
    fflush(stderr);                                                         \
    sb_abort(0);                                                            \
  }                                                                         \
} while (0)
#else
# define sb_assert(EXPR)
#endif

#define sb_abort(FLAG)  \
  sb_internal_abort(__FILE__, __LINE__, FLAG)

/*--------------------------------------------------------------------------*/


#define SB_TO_SYS(NPAGES, PAGESIZE) \
  ((NPAGES)*(PAGESIZE)/sysconf(_SC_PAGESIZE))

#define SB_INIT_CHECK     \
  if (0 == sb_info.init)  \
    sb_internal_init();

#define SBISSET(FLAGS, FLAG) \
  ((FLAG) == ((FLAGS)&(FLAG)))

//#ifdef USE_PTHREAD
//# define MMAP_FLAGS MAP_SHARED|MAP_ANONYMOUS|MAP_NORESERVE
//#else
# define MMAP_FLAGS MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE
//#endif
#define SBMMAP(ADDR, LEN, PROT)                                               \
  do {                                                                        \
    (ADDR) = (size_t)mmap(NULL, LEN, PROT, MMAP_FLAGS, -1, 0);                \
    if (MAP_FAILED == (void *)(ADDR))                                         \
      sb_abort(1);                                                            \
  } while (0)

#define SBMUNMAP(ADDR, LEN)                                                 \
do {                                                                        \
  if (-1 == munmap((void*)(ADDR), LEN))                                     \
    sb_abort(1);                                                            \
} while (0)

#define SBMLOCK(ADDR, LEN)                                                  \
do {                                                                        \
  /*if (-1 == libc_mlock((void*)(ADDR), LEN))                                 \
    sb_abort(1);*/                                                            \
} while (0)

#define SBMUNLOCK(ADDR, LEN)                                                \
do {                                                                        \
  /*if (-1 == libc_munlock((void*)(ADDR), LEN))                               \
    sb_abort(1);*/                                                            \
} while (0)

#define SBMADVISE(ADDR, LEN, FLAG)                                          \
do {                                                                        \
  if (-1 == madvise((void *)(ADDR), LEN, FLAG))                             \
    sb_abort(1);                                                            \
} while (0)

#define SBFADVISE(FD, OFF, LEN, FLAG)                                       \
do {                                                                        \
  if (-1 == posix_fadvise(FD, OFF, LEN, FLAG))                              \
    sb_abort(1);                                                            \
} while (0)

#define SBMPROTECT(ADDR, LEN, PROT)                                         \
do {                                                                        \
  if (-1 == mprotect((void*)(ADDR), LEN, PROT))                             \
    sb_abort(1);                                                            \
} while (0)

#endif
