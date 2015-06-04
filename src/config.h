#ifndef __CONFIG_H__
#define __CONFIG_H__ 1


#include <sys/types.h> /* ssize_t */


/****************************************************************************/
/*! Pthread configurations. */
/****************************************************************************/
#ifdef USE_PTHREAD
# include <pthread.h> /* pthread library */
# define LOCK_INIT(LOCK) pthread_mutex_init(LOCK, NULL)
# define LOCK_FREE(LOCK) pthread_mutex_destroy(LOCK)
# define LOCK_GET(LOCK)  pthread_mutex_lock(LOCK)
# define LOCK_LET(LOCK)  pthread_mutex_unlock(LOCK)
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
extern int     libc_close(int);
extern ssize_t libc_read(int const, void * const, size_t const);
extern ssize_t libc_write(int const, void const * const, size_t const);
extern int     libc_mlock(void const * const, size_t const);
extern int     libc_munlock(void const * const, size_t const);

#ifdef __cplusplus
}
#endif


#endif
