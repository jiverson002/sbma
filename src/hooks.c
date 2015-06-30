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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif


#ifdef NDEBUG
# undef NDEBUG
#endif


/****************************************************************************/
/* Need optimizations off or GCC will optimize away the temporary setting of
 * libc_calloc in the HOOK_INIT macro. */
/****************************************************************************/
#pragma GCC push_options
#pragma GCC optimize("O0")


#include <dlfcn.h>     /* dlsym */
#include <fcntl.h>     /* open */
#include <malloc.h>    /* struct mallinfo */
#include <stdarg.h>    /* stdarg library */
#include <stddef.h>    /* size_t */
#include <stdio.h>     /* FILE */
#include <string.h>    /* memset */
#include <sys/stat.h>  /* stat, open */
#include <sys/types.h> /* stat, open */
#include <unistd.h>    /* ssize_t, stat */

#include "config.h"
#include "ipc.h"
#include "sbma.h"
#include "vmm.h"


/****************************************************************************/
/* required function prototypes */
/****************************************************************************/
extern int sbma_eligible(int);


/****************************************************************************/
/* need to provide temporary calloc function for dlsym */
/****************************************************************************/
#define HOOK_INIT(func)                                                     \
do {                                                                        \
  if (NULL == _libc_calloc) {                                               \
    _libc_calloc = internal_calloc;                                         \
    *((void **) &_libc_calloc) = dlsym(RTLD_NEXT, "calloc");                \
    ASSERT(NULL != _libc_calloc);                                           \
  }                                                                         \
  if (NULL == _libc_##func) {                                               \
    *((void **) &_libc_##func) = dlsym(RTLD_NEXT, #func);                   \
    ASSERT(NULL != _libc_##func);                                           \
  }                                                                         \
} while (0)


/****************************************************************************/
/* memory and function pointer for internal_calloc */
/****************************************************************************/
static void *(*_libc_calloc)(size_t, size_t)=NULL;
static char internal_calloc_mem[1024*1024]={0};
static char * internal_calloc_ptr=internal_calloc_mem;


/****************************************************************************/
/*! Temporary calloc function to be used only for dlsym.  After the
 *  libc_calloc is populated using dlsym, this function will never be called
 *  again. */
/****************************************************************************/
static void *
internal_calloc(size_t const num, size_t const size)
{
  char * nptr=NULL;

  if (internal_calloc_ptr+(num*size) <= internal_calloc_mem+(1024*1024)) {
    nptr = internal_calloc_ptr;
    memset(nptr, 0, num*size);
    internal_calloc_ptr += (num*size);
  }

  return nptr;
}


/*************************************************************************/
/*! Hook: libc malloc */
/*************************************************************************/
extern void *
libc_malloc(size_t const size)
{
  static void * (*_libc_malloc)(size_t)=NULL;

  HOOK_INIT(malloc);

  return _libc_malloc(size);
}


/*************************************************************************/
/*! Hook: libc calloc */
/*************************************************************************/
extern void *
libc_calloc(size_t const num, size_t const size)
{
  HOOK_INIT(calloc);

  return _libc_calloc(num, size);
}


/*************************************************************************/
/*! Hook: libc realloc */
/*************************************************************************/
extern void *
libc_realloc(void * const ptr, size_t const size)
{
  static void * (*_libc_realloc)(void* const, size_t)=NULL;

  HOOK_INIT(realloc);

  return _libc_realloc(ptr, size);
}


/*************************************************************************/
/*! Hook: libc free */
/*************************************************************************/
extern void
libc_free(void * const ptr)
{
  static void * (*_libc_free)(void* const)=NULL;

  HOOK_INIT(free);

  _libc_free(ptr);
}


/*************************************************************************/
/*! Hook: libc stat */
/*************************************************************************/
extern int
libc_stat(char const * path, struct stat * buf)
{
  static ssize_t (*_libc_stat)(char const*, struct stat*)=NULL;

  HOOK_INIT(stat);

  return _libc_stat(path, buf);
}


/*************************************************************************/
/*! Hook: libc __xstat */
/*************************************************************************/
extern int
libc___xstat(int ver, char const * path, struct stat * buf)
{
  static ssize_t (*_libc___xstat)(int, char const*, struct stat*)=NULL;

  HOOK_INIT(__xstat);

  return _libc___xstat(ver, path, buf);
}


#if 0
/*************************************************************************/
/*! Hook: libc __xstat64 */
/*************************************************************************/
extern int
#ifdef __USE_LARGEFILE64
libc_xstat64(int ver, char const * path, struct stat64 * buf)
#else
libc_xstat64(int ver, char const * path, struct stat * buf)
#endif
{
#ifdef __USE_LARGEFILE64
  static ssize_t (*_libc_xstat64)(int, char const*, struct stat64*)=NULL;
#else
  static ssize_t (*_libc_xstat64)(int, char const*, struct stat*)=NULL;
#endif

  HOOK_INIT(xstat64);

  return _libc_xstat64(ver, path, buf);
}
#endif


/*************************************************************************/
/*! Hook: libc open */
/*************************************************************************/
extern int
libc_open(char const * path, int flags, ...)
{
  static ssize_t (*_libc_open)(char const*, int, ...)=NULL;
  va_list list;
  mode_t mode=0;

  HOOK_INIT(open);

  if (O_CREAT == (flags&O_CREAT)) {
    va_start(list, flags);
    mode = va_arg(list, mode_t);
  }

  return _libc_open(path, flags, mode);
}


/*************************************************************************/
/*! Hook: libc read */
/*************************************************************************/
extern ssize_t
libc_read(int const fd, void * const buf, size_t const count)
{
  static ssize_t (*_libc_read)(int, void*, size_t)=NULL;

  HOOK_INIT(read);

  return _libc_read(fd, buf, count);
}


/*************************************************************************/
/*! Hook: libc write */
/*************************************************************************/
extern ssize_t
libc_write(int const fd, void const * const buf, size_t const count)
{
  static ssize_t (*_libc_write)(int, void const*, size_t)=NULL;

  HOOK_INIT(write);

  return _libc_write(fd, buf, count);
}


/*************************************************************************/
/*! Hook: libc fread */
/*************************************************************************/
extern size_t
libc_fread(void * const buf, size_t const size, size_t const num,
      FILE * const stream)
{
  static size_t (*_libc_fread)(void*, size_t, size_t, FILE *)=NULL;

  HOOK_INIT(fread);

  return _libc_fread(buf, size, num, stream);
}


/*************************************************************************/
/*! Hook: libc fwrite */
/*************************************************************************/
extern size_t
libc_fwrite(void const * const buf, size_t const size, size_t const num,
       FILE * const stream)
{
  static size_t (*_libc_fwrite)(void const*, size_t, size_t, FILE *)=NULL;

  HOOK_INIT(fwrite);

  return _libc_fwrite(buf, size, num, stream);
}


/*************************************************************************/
/*! Hook: libc mlock */
/*************************************************************************/
extern int
libc_mlock(void const * const addr, size_t const len)
{
  static int (*_libc_mlock)(void const*, size_t)=NULL;

  HOOK_INIT(mlock);

  return _libc_mlock(addr, len);
}


/*************************************************************************/
/*! Hook: libc mlockall */
/*************************************************************************/
extern int
libc_mlockall(int flags)
{
  static int (*_libc_mlockall)(int)=NULL;

  HOOK_INIT(mlockall);

  return _libc_mlockall(flags);
}


/****************************************************************************/
/*! Hook: libc msync */
/****************************************************************************/
extern int
libc_msync(void * const addr, size_t const len, int const flags)
{
  static int (*_libc_msync)(void*, size_t, int)=NULL;

  HOOK_INIT(msync);

  return _libc_msync(addr, len, flags);
}


#ifdef USE_PTHREAD
/****************************************************************************/
/*! Hook: libc sem_wait */
/****************************************************************************/
extern int
libc_sem_wait(sem_t * const sem)
{
  static int (*_libc_sem_wait)(sem_t *)=NULL;

  HOOK_INIT(sem_wait);

  return _libc_sem_wait(sem);
}


/****************************************************************************/
/*! Hook: libc sem_timedwait */
/****************************************************************************/
extern int
libc_sem_timedwait(sem_t * const sem, struct timespec const * const ts)
{
  static int (*_libc_sem_timedwait)(sem_t *, struct timespec const *)=NULL;

  HOOK_INIT(sem_timedwait);

  return _libc_sem_timedwait(sem, ts);
}


/****************************************************************************/
/*! Hook: libc mq_send */
/****************************************************************************/
extern ssize_t
libc_mq_send(mqd_t const mqdes, char const * const msg_ptr,
             size_t const msg_len, unsigned const msg_prio)
{
  static int (*_libc_mq_send)(mqd_t, char const *, size_t, unsigned)=NULL;

  HOOK_INIT(mq_send);

  return _libc_mq_send(mqdes, msg_ptr, msg_len, msg_prio);
}


/****************************************************************************/
/*! Hook: libc mq_timedsend */
/****************************************************************************/
extern ssize_t
libc_mq_timedsend(mqd_t const mqdes, char const * const msg_ptr,
                  size_t const msg_len, unsigned const msg_prio,
                  struct timespec const * const abs_timeout)
{
  static int (*_libc_mq_timedsend)(mqd_t, char const *, size_t, unsigned,\
    struct timespec const *)=NULL;

  HOOK_INIT(mq_timedsend);

  return _libc_mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio,\
    abs_timeout);
}


/****************************************************************************/
/*! Hook: libc mq_receive */
/****************************************************************************/
extern ssize_t
libc_mq_receive(mqd_t const mqdes, char * const msg_ptr, size_t const msg_len,
                unsigned * const msg_prio)
{
  static int (*_libc_mq_receive)(mqd_t, char *, size_t, unsigned *)=NULL;

  HOOK_INIT(mq_receive);

  return _libc_mq_receive(mqdes, msg_ptr, msg_len, msg_prio);
}


/****************************************************************************/
/*! Hook: libc mq_timedreceive */
/****************************************************************************/
extern ssize_t
libc_mq_timedreceive(mqd_t const mqdes, char * const msg_ptr,
                     size_t const msg_len, unsigned * const msg_prio,
                     struct timespec const * const abs_timeout)
{
  static int (*_libc_mq_timedreceive)(mqd_t, char *, size_t, unsigned *,\
    struct timespec const *)=NULL;

  HOOK_INIT(mq_timedreceive);

  return _libc_mq_timedreceive(mqdes, msg_ptr, msg_len, msg_prio,\
    abs_timeout);
}
#endif


//#define USE_LIBC
/*************************************************************************/
/*! Hook: malloc */
/*************************************************************************/
extern void *
malloc(size_t const size)
{
  HOOK_INIT(calloc);

/*#ifdef USE_LIBC
  void * ptr = libc_malloc(size);
#else
  void * ptr = SBMA_malloc(size);
#endif
  printf("m %p %zu\n", ptr, size);
  return ptr;*/

#ifdef USE_LIBC
  return libc_malloc(size);
#else
  return SBMA_malloc(size);
#endif
}


/*************************************************************************/
/*! Hook: calloc */
/*************************************************************************/
extern void *
calloc(size_t const num, size_t const size)
{
  HOOK_INIT(calloc);

  if (internal_calloc == _libc_calloc)
    return internal_calloc(num, size);

#ifdef USE_LIBC
  return libc_calloc(num, size);
#else
  return SBMA_calloc(num, size);
#endif
}


/*************************************************************************/
/*! Hook: realloc */
/*************************************************************************/
extern void *
realloc(void * const ptr, size_t const size)
{
  HOOK_INIT(calloc);

  if (NULL == ptr)
    return SBMA_malloc(size);

#ifdef USE_LIBC
  return libc_realloc(ptr, size);
#else
  return SBMA_realloc(ptr, size);
#endif
}


/*************************************************************************/
/*! Hook: free */
/*************************************************************************/
extern void
free(void * const ptr)
{
  HOOK_INIT(calloc);

  if (NULL == ptr)
    return;

  /* skip internal_calloc allocations */
  if ((char*)ptr >= internal_calloc_mem &&
    (char*)ptr < internal_calloc_mem+1024*1024)
  {
    return;
  }

  /*printf("f %p\n", ptr);*/

#ifdef USE_LIBC
  libc_free(ptr);
#else
  SBMA_free(ptr);
#endif
}


/*************************************************************************/
/*! Hook: mallinfo */
/*************************************************************************/
extern struct mallinfo
mallinfo(void)
{
  HOOK_INIT(calloc);

  return SBMA_mallinfo();
}


/*************************************************************************/
/*! Hook: stat */
/*************************************************************************/
extern int
stat(char const * path, struct stat * buf)
{
  if (1 == SBMA_mexist(path))
    (void)SBMA_mtouch((void*)path, strlen(path));
  if (1 == SBMA_mexist(buf))
    (void)SBMA_mtouch((void*)buf, sizeof(struct stat));

  return libc_stat(path, buf);
}


/*************************************************************************/
/*! Hook: __xstat */
/*************************************************************************/
extern int
__xstat(int ver, const char * path, struct stat * buf)
{
  if (1 == SBMA_mexist(path))
    (void)SBMA_mtouch((void*)path, strlen(path));
  if (1 == SBMA_mexist(buf))
    (void)SBMA_mtouch((void*)buf, sizeof(struct stat));

  return libc___xstat(ver, path, buf);
}


#if 0
# ifdef __USE_LARGEFILE64
/*************************************************************************/
/*! Hook: __xstat64 */
/*************************************************************************/
extern int
__xstat64(int ver, const char * path, struct stat64 * buf)
{
  if (1 == SBMA_mexist(path))
    (void)SBMA_mtouch((void*)path, strlen(path));

  return libc_xstat64(ver, path, buf);
}
#endif
#endif


/*************************************************************************/
/*! Hook: stat */
/*************************************************************************/
extern int
open(char const * path, int flags, ...)
{
  va_list list;
  mode_t mode=0;

  if (1 == SBMA_mexist(path))
    (void)SBMA_mtouch((void*)path, strlen(path));

  if (O_CREAT == (flags&O_CREAT)) {
    va_start(list, flags);
    mode = va_arg(list, mode_t);
  }

  return libc_open(path, flags, mode);
}


/*************************************************************************/
/*! Hook: read */
/*************************************************************************/
extern ssize_t
read(int const fd, void * const buf, size_t const count)
{
  /* NOTE: Consider the following execution sequence. During the call to
   * memset, the first n pages of buf are loaded, then the process must wait
   * because the system cannot support any additional memory. While waiting,
   * the process receives a SIGIPC and evicts all of the memory which it had
   * previously admitted. When memset finishes, buf[0, count) is not all
   * resident. This is an error. A hack to address this is to first call
   * SBMA_mtouch which does a single load for the whole range. However, this
   * is a larger issue which should be addressed at some point. */
  if (1 == SBMA_mexist(buf)) {
    /* NOTE: memset() must be used instead of SBMA_mtouch() for the following
     * reason. If the relevant memory page has been written to disk and thus,
     * given no R/W permissions, then using SBMA_mtouch() with SBPAGE_DIRTY
     * will give the relevant page appropriate permissions, however, it will
     * cause the page not be read from disk. This is incorrect if the page is
     * a shared page, since then any data that was in the shared page, but not
     * part of the relevant memory, will be lost. */
    (void)SBMA_mtouch(buf, count);
    memset(buf, 0, count);
  }

  return libc_read(fd, buf, count);
}


/*************************************************************************/
/*! Hook: write */
/*************************************************************************/
extern ssize_t
write(int const fd, void const * const buf, size_t const count)
{
  if (1 == SBMA_mexist(buf))
    (void)SBMA_mtouch((void*)buf, count);

  return libc_write(fd, buf, count);
}


/*************************************************************************/
/*! Hook: fread */
/*************************************************************************/
extern size_t
fread(void * const buf, size_t const size, size_t const num,
      FILE * const stream)
{
  if (1 == SBMA_mexist(buf)) {
    /* NOTE: For an explaination of why memset() must be used instead of
     * SBMA_mtouch(), see discussion in read(). */
    (void)SBMA_mtouch(buf, size*num);
    memset(buf, 0, size*num);
  }


  return libc_fread(buf, size, num, stream);
}


/*************************************************************************/
/*! Hook: fwrite */
/*************************************************************************/
extern size_t
fwrite(void const * const buf, size_t const size, size_t const num,
       FILE * const stream)
{
  if (1 == SBMA_mexist(buf))
    (void)SBMA_mtouch((void*)buf, size);

  return libc_fwrite(buf, size, num, stream);
}


/*************************************************************************/
/*! Hook: mlock */
/*************************************************************************/
extern int
mlock(void const * const addr, size_t const len)
{
  (void)SBMA_mtouch((void*)addr, len);

  return libc_mlock(addr, len);
}


/*************************************************************************/
/*! Hook: mlockall */
/*************************************************************************/
extern int
mlockall(int flags)
{
  (void)SBMA_mtouchall();

  return libc_mlockall(flags);
}


/****************************************************************************/
/*! Hook: msync */
/****************************************************************************/
extern int
msync(void * const addr, size_t const len, int const flags)
{
  if (0 == SBMA_mexist(addr))
    return libc_msync(addr, len, flags);
  else
    return SBMA_mevict(addr, len);
}


#ifdef USE_PTHREAD
/****************************************************************************/
/*! Hook: sem_wait */
/****************************************************************************/
extern int
sem_wait(sem_t * const sem)
{
  int ret;

  /* try to wait before becoming eligible */
  ret = sem_trywait(sem);
  if (-1 == ret) {
    if (EAGAIN == errno)
      errno = 0;
    else
      return -1;
  }
  else {
    return ret;
  }

  ret = sbma_eligible(IPC_ELIGIBLE);
  if (-1 == ret)
    return -1;

  for (;;) {
    ret = libc_sem_wait(sem);
    if (-1 == ret) {
      if (EINTR == errno) {
        errno = 0;
      }
      else {
        (void)sbma_eligible(0);
        return -1;
      }
    }
    else {
      break;
    }
  }

  ret = sbma_eligible(0);
  if (-1 == ret)
    return -1;

  return 0;
}


/****************************************************************************/
/*! Hook: sem_timedwait */
/****************************************************************************/
extern int
sem_timedwait(sem_t * const sem, struct timespec const * const ts)
{
  int ret;

  /* try to wait before becoming eligible */
  ret = sem_trywait(sem);
  if (-1 == ret) {
    if (EAGAIN == errno)
      errno = 0;
    else
      return -1;
  }
  else {
    return ret;
  }

  ret = sbma_eligible(IPC_ELIGIBLE);
  if (-1 == ret)
    return -1;

  for (;;) {
    ret = libc_sem_timedwait(sem, ts);
    if (-1 == ret) {
      if (EINTR == errno) {
        errno = 0;
      }
      else {
        (void)sbma_eligible(0);
        return -1;
      }
    }
    else {
      break;
    }
  }

  ret = sbma_eligible(0);
  if (-1 == ret)
    return -1;

  return 0;
}


/****************************************************************************/
/*! Hook: mq_send */
/****************************************************************************/
extern int
mq_send(mqd_t const mqdes, char const * const msg_ptr, size_t const msg_len,
        unsigned const msg_prio)
{
  int ret;
  struct mq_attr oldattr, newattr;

  /* get current mq attributes */
  ret = mq_getattr(mqdes, &oldattr);
  if (-1 == ret)
    return -1;

  /* set mq as non-blocking to test for message */
  if (O_NONBLOCK != (oldattr.mq_flags&O_NONBLOCK)) {
    memcpy(&newattr, &oldattr, sizeof(struct mq_attr));
    newattr.mq_flags = O_NONBLOCK;
    ret = mq_setattr(mqdes, &newattr, &oldattr);
    if (-1 == ret)
      return -1;
  }

  /* check if message can be received */
  ret = libc_mq_send(mqdes, msg_ptr, msg_len, msg_prio);
  if (-1 == ret) {
    if (EAGAIN == errno)
      errno = 0;
    else
      return -1;
  }
  else {
    return 0;
  }

  /* reset mq attributes if necessary */
  if (O_NONBLOCK != (oldattr.mq_flags&O_NONBLOCK)) {
    ret = mq_setattr(mqdes, &oldattr, NULL);
    if (-1 == ret)
      return -1;
  }

  /* set as eligible and make possibly blocking library call */
  ret = sbma_eligible(IPC_ELIGIBLE);
  if (-1 == ret)
    return -1;

  for (;;) {
    ret = libc_mq_send(mqdes, msg_ptr, msg_len, msg_prio);
    if (-1 == ret) {
      if (EINTR == errno) {
        errno = 0;
      }
      else {
        (void)sbma_eligible(0);
        return -1;
      }
    }
    else {
      break;
    }
  }

  ret = sbma_eligible(0);
  if (-1 == ret)
    return -1;

  return 0;
}


/****************************************************************************/
/*! Hook: mq_timedsend */
/****************************************************************************/
extern int
mq_timedsend(mqd_t const mqdes, char const * const msg_ptr,
             size_t const msg_len, unsigned const msg_prio,
             struct timespec const * const abs_timeout)
{
  int ret;
  struct mq_attr oldattr, newattr;

  /* get current mq attributes */
  ret = mq_getattr(mqdes, &oldattr);
  if (-1 == ret)
    return -1;

  /* set mq as non-blocking to test for message */
  if (O_NONBLOCK != (oldattr.mq_flags&O_NONBLOCK)) {
    memcpy(&newattr, &oldattr, sizeof(struct mq_attr));
    newattr.mq_flags = O_NONBLOCK;
    ret = mq_setattr(mqdes, &newattr, &oldattr);
    if (-1 == ret)
      return -1;
  }

  /* check if message can be received */
  ret = libc_mq_send(mqdes, msg_ptr, msg_len, msg_prio);
  if (-1 == ret) {
    if (EAGAIN == errno)
      errno = 0;
    else
      return -1;
  }
  else {
    return 0;
  }

  /* reset mq attributes if necessary */
  if (O_NONBLOCK != (oldattr.mq_flags&O_NONBLOCK)) {
    ret = mq_setattr(mqdes, &oldattr, NULL);
    if (-1 == ret)
      return -1;
  }

  /* set as eligible and make possibly blocking library call */
  ret = sbma_eligible(IPC_ELIGIBLE);
  if (-1 == ret)
    return -1;

  for (;;) {
    ret = libc_mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio, abs_timeout);
    if (-1 == ret) {
      if (EINTR == errno) {
        errno = 0;
      }
      else {
        (void)sbma_eligible(0);
        return -1;
      }
    }
    else {
      break;
    }
  }

  ret = sbma_eligible(0);
  if (-1 == ret)
    return -1;

  return 0;
}


/****************************************************************************/
/*! Hook: mq_receive */
/****************************************************************************/
extern ssize_t
mq_receive(mqd_t const mqdes, char * const msg_ptr, size_t const msg_len,
           unsigned * const msg_prio)
{
  int ret;
  ssize_t retval;
  struct mq_attr oldattr, newattr;

  /* get current mq attributes */
  ret = mq_getattr(mqdes, &oldattr);
  if (-1 == ret)
    return -1;

  /* set mq as non-blocking to test for message */
  if (O_NONBLOCK != (oldattr.mq_flags&O_NONBLOCK)) {
    memcpy(&newattr, &oldattr, sizeof(struct mq_attr));
    newattr.mq_flags = O_NONBLOCK;
    ret = mq_setattr(mqdes, &newattr, &oldattr);
    if (-1 == ret)
      return -1;
  }

  /* check if message can be received */
  retval = libc_mq_receive(mqdes, msg_ptr, msg_len, msg_prio);
  if (-1 == retval) {
    if (EAGAIN == errno)
      errno = 0;
    else
      return -1;
  }
  else {
    return retval;
  }

  /* reset mq attributes if necessary */
  if (O_NONBLOCK != (oldattr.mq_flags&O_NONBLOCK)) {
    ret = mq_setattr(mqdes, &oldattr, NULL);
    if (-1 == ret)
      return -1;
  }

  /* set as eligible and make possibly blocking library call */
  ret = sbma_eligible(IPC_ELIGIBLE);
  if (-1 == ret)
    return -1;

  for (;;) {
    retval = libc_mq_receive(mqdes, msg_ptr, msg_len, msg_prio);
    if (-1 == retval) {
      if (EINTR == errno) {
        errno = 0;
      }
      else {
        (void)sbma_eligible(0);
        return -1;
      }
    }
    else {
      break;
    }
  }

  ret = sbma_eligible(0);
  if (-1 == ret)
    return -1;

  return retval;
}


/****************************************************************************/
/*! Hook: mq_timedreceive */
/****************************************************************************/
extern ssize_t
mq_timedreceive(mqd_t const mqdes, char * const msg_ptr, size_t const msg_len,
                unsigned * const msg_prio,
                struct timespec const * const abs_timeout)
{
  int ret;
  ssize_t reval;
  struct mq_attr oldattr, newattr;

  /* get current mq attributes */
  ret = mq_getattr(mqdes, &oldattr);
  if (-1 == ret)
    return -1;

  /* set mq as non-blocking to test for message */
  if (O_NONBLOCK != (oldattr.mq_flags&O_NONBLOCK)) {
    memcpy(&newattr, &oldattr, sizeof(struct mq_attr));
    newattr.mq_flags = O_NONBLOCK;
    ret = mq_setattr(mqdes, &newattr, &oldattr);
    if (-1 == ret)
      return -1;
  }

  /* check if message can be received */
  retval = libc_mq_receive(mqdes, msg_ptr, msg_len, msg_prio);
  if (-1 == retval) {
    if (EAGAIN == errno)
      errno = 0;
    else
      return -1;
  }
  else {
    return retval;
  }

  /* reset mq attributes if necessary */
  if (O_NONBLOCK != (oldattr.mq_flags&O_NONBLOCK)) {
    ret = mq_setattr(mqdes, &oldattr, NULL);
    if (-1 == ret)
      return -1;
  }

  /* set as eligible and make possibly blocking library call */
  ret = sbma_eligible(IPC_ELIGIBLE);
  if (-1 == ret)
    return -1;

  for (;;) {
    retval = libc_mq_timedreceive(mqdes, msg_ptr, msg_len, msg_prio,\
      abs_timeout);
    if (-1 == retval) {
      if (EINTR == errno) {
        errno = 0;
      }
      else {
        (void)sbma_eligible(0);
        return -1;
      }
    }
    else {
      break;
    }
  }

  ret = sbma_eligible(0);
  if (-1 == ret)
    return -1;

  return retval;
}
#endif


#pragma GCC pop_options
