#ifndef __CONFIG_H__
#define __CONFIG_H__ 1


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


#endif
