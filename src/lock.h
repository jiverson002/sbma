/*
Copyright (c) 2015 Jeremy Iverson

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


#ifndef SBMA_LOCK_H
#define SBMA_LOCK_H 1


/*****************************************************************************/
/*  Pthread configurations. */
/*****************************************************************************/
#ifdef USE_THREAD
# include <pthread.h> /* pthread library */

# define DEADLOCK 0   /* 0: no deadlock diagnostics, */
                      /* 1: deadlock diagnostics */

# define lock_get(LOCK) lock_get_int(__func__, __LINE__, #LOCK, LOCK)
# define lock_let(LOCK) lock_let_int(__func__, __LINE__, #LOCK, LOCK)

# ifdef __cplusplus
extern "C" {
# endif

/*****************************************************************************/
/*  Initialize pthread lock. */
/*****************************************************************************/
int
lock_init(pthread_mutex_t * const lock);


/*****************************************************************************/
/*  Destroy pthread lock. */
/*****************************************************************************/
int
lock_free(pthread_mutex_t * const lock);


/*****************************************************************************/
/*  Lock pthread lock. */
/*****************************************************************************/
int
lock_get_int(char const * const func, int const line,
             char const * const lock_str, pthread_mutex_t * const lock);


/*****************************************************************************/
/*  Unlock pthread lock. */
/*****************************************************************************/
int
lock_let_int(char const * const func, int const line,
             char const * const lock_str, pthread_mutex_t * const lock);

# ifdef __cplusplus
}
# endif
#else
# define lock_init(...) 0
# define lock_free(...) 0
# define lock_get(...)  0
# define lock_let(...)  0
#endif


#endif /* SBMA_LOCK_H */
