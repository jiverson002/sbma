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

#ifndef __LOCK_H__
#define __LOCK_H__ 1


/****************************************************************************/
/*! Pthread configurations. */
/****************************************************************************/
#ifdef USE_THREAD
# include <pthread.h> /* pthread library */

# define DEADLOCK 0   /* 0: no deadlock diagnostics, */
                      /* 1: deadlock diagnostics */

# define __lock_get(LOCK) __lock_get_int(__func__, __LINE__, #LOCK, LOCK)
# define __lock_let(LOCK) __lock_let_int(__func__, __LINE__, #LOCK, LOCK)

# ifdef __cplusplus
extern "C" {
# endif

/****************************************************************************/
/*! Initialize pthread lock. */
/****************************************************************************/
int
__lock_init(pthread_mutex_t * const __lock);


/****************************************************************************/
/*! Destroy pthread lock. */
/****************************************************************************/
int
__lock_free(pthread_mutex_t * const __lock);


/****************************************************************************/
/*! Lock pthread lock. */
/****************************************************************************/
int
__lock_get_int(char const * const __func, int const __line,
               char const * const __lock_str, pthread_mutex_t * const __lock);


/****************************************************************************/
/*! Unlock pthread lock. */
/****************************************************************************/
int
__lock_let_int(char const * const __func, int const __line,
               char const * const __lock_str, pthread_mutex_t * const __lock);

# ifdef __cplusplus
}
# endif
#else
# define __lock_init(...) 0
# define __lock_free(...) 0
# define __lock_get(...)  0
# define __lock_let(...)  0
#endif


#endif
