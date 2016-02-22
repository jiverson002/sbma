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
# include <pthread.h>     /* pthread library */
# include "common.h"


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
#else
/* Required incase USE_THREAD is not defined, so that this is not an empty
 * translation unit. */
typedef int make_iso_compilers_happy;
#endif
