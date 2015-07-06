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


/****************************************************************************/
/* Need optimizations off or GCC will optimize away the temporary setting of
 * libc_calloc in the HOOK_INIT macro. */
/****************************************************************************/
#pragma GCC push_options
#pragma GCC optimize("O0")


#include <dlfcn.h>     /* dlsym */
#include <errno.h>     /* errno library */
#include <fcntl.h>     /* open */
#include <malloc.h>    /* struct mallinfo */
#include <mqueue.h>    /* mq_*send, mq_*receive */
#include <semaphore.h> /* semaphore library */
#include <stdarg.h>    /* stdarg library */
#include <stddef.h>    /* size_t */
#include <stdio.h>     /* FILE */
#include <string.h>    /* memset, memcpy */
#include <sys/stat.h>  /* stat, open */
#include <sys/types.h> /* stat, open */
#include <unistd.h>    /* ssize_t, stat */
#include "config.h"
#include "ipc.h"
#include "sbma.h"
#include "vmm.h"


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
SBMA_STATIC void *
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
SBMA_EXTERN void *
libc_malloc(size_t const size)
{
  static void * (*_libc_malloc)(size_t)=NULL;

  HOOK_INIT(malloc);

  return _libc_malloc(size);
}
SBMA_EXPORT(internal, void *
libc_malloc(size_t const size));


/*************************************************************************/
/*! Hook: libc calloc */
/*************************************************************************/
SBMA_EXTERN void *
libc_calloc(size_t const num, size_t const size)
{
  HOOK_INIT(calloc);

  return _libc_calloc(num, size);
}
SBMA_EXPORT(internal, void *
libc_calloc(size_t const num, size_t const size));


/*************************************************************************/
/*! Hook: libc realloc */
/*************************************************************************/
SBMA_EXTERN void *
libc_realloc(void * const ptr, size_t const size)
{
  static void * (*_libc_realloc)(void* const, size_t)=NULL;

  HOOK_INIT(realloc);

  return _libc_realloc(ptr, size);
}
SBMA_EXPORT(internal, void *
libc_realloc(void * const ptr, size_t const size));


/*************************************************************************/
/*! Hook: libc free */
/*************************************************************************/
SBMA_EXTERN void
libc_free(void * const ptr)
{
  static void (*_libc_free)(void* const)=NULL;

  HOOK_INIT(free);

  _libc_free(ptr);
}
SBMA_EXPORT(internal, void
libc_free(void * const ptr));


/*************************************************************************/
/*! Hook: libc memcpy */
/*************************************************************************/
SBMA_EXTERN void *
libc_memcpy(void * const dst, void const * const src, size_t const num)
{
  static void * (*_libc_memcpy)(void*, void const*, size_t)=NULL;

  HOOK_INIT(memcpy);

  return _libc_memcpy(dst, src, num);
}
SBMA_EXPORT(internal, void *
libc_memcpy(void * const dst, void const * const src, size_t const num));


/*************************************************************************/
/*! Hook: libc memmove */
/*************************************************************************/
SBMA_EXTERN void *
libc_memmove(void * const dst, void const * const src, size_t const num)
{
  static void * (*_libc_memmove)(void*, void const*, size_t)=NULL;

  HOOK_INIT(memmove);

  return _libc_memmove(dst, src, num);
}
SBMA_EXPORT(internal, void *
libc_memmove(void * const dst, void const * const src, size_t const num));


/*************************************************************************/
/*! Hook: libc stat */
/*************************************************************************/
SBMA_EXTERN int
libc_stat(char const * path, struct stat * buf)
{
  static ssize_t (*_libc_stat)(char const*, struct stat*)=NULL;

  HOOK_INIT(stat);

  return _libc_stat(path, buf);
}
SBMA_EXPORT(internal, int
libc_stat(char const * path, struct stat * buf));


/*************************************************************************/
/*! Hook: libc __xstat */
/*************************************************************************/
SBMA_EXTERN int
libc___xstat(int ver, char const * path, struct stat * buf)
{
  static ssize_t (*_libc___xstat)(int, char const*, struct stat*)=NULL;

  HOOK_INIT(__xstat);

  return _libc___xstat(ver, path, buf);
}
SBMA_EXPORT(internal, int
libc___xstat(int ver, char const * path, struct stat * buf));


/*************************************************************************/
/*! Hook: libc __xstat64 */
/*************************************************************************/
#if 0
SBMA_EXTERN int
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
#ifdef __USE_LARGEFILE64
SBMA_EXPORT(internal, int
libc_xstat64(int ver, char const * path, struct stat64 * buf));
#else
SBMA_EXPORT(internal, int
libc_xstat64(int ver, char const * path, struct stat * buf));
#endif
#endif


/*************************************************************************/
/*! Hook: libc open */
/*************************************************************************/
SBMA_EXTERN int
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
SBMA_EXPORT(internal, int
libc_open(char const * path, int flags, ...));


/*************************************************************************/
/*! Hook: libc read */
/*************************************************************************/
SBMA_EXTERN ssize_t
libc_read(int const fd, void * const buf, size_t const count)
{
  static ssize_t (*_libc_read)(int, void*, size_t)=NULL;

  HOOK_INIT(read);

  return _libc_read(fd, buf, count);
}
SBMA_EXPORT(internal, ssize_t
libc_read(int const fd, void * const buf, size_t const count));


/*************************************************************************/
/*! Hook: libc write */
/*************************************************************************/
SBMA_EXTERN ssize_t
libc_write(int const fd, void const * const buf, size_t const count)
{
  static ssize_t (*_libc_write)(int, void const*, size_t)=NULL;

  HOOK_INIT(write);

  return _libc_write(fd, buf, count);
}
SBMA_EXPORT(internal, ssize_t
libc_write(int const fd, void const * const buf, size_t const count));


/*************************************************************************/
/*! Hook: libc fread */
/*************************************************************************/
SBMA_EXTERN size_t
libc_fread(void * const buf, size_t const size, size_t const num,
           FILE * const stream)
{
  static size_t (*_libc_fread)(void*, size_t, size_t, FILE *)=NULL;

  HOOK_INIT(fread);

  return _libc_fread(buf, size, num, stream);
}
SBMA_EXPORT(internal, size_t
libc_fread(void * const buf, size_t const size, size_t const num,
           FILE * const stream));


/*************************************************************************/
/*! Hook: libc fwrite */
/*************************************************************************/
SBMA_EXTERN size_t
libc_fwrite(void const * const buf, size_t const size, size_t const num,
            FILE * const stream)
{
  static size_t (*_libc_fwrite)(void const*, size_t, size_t, FILE *)=NULL;

  HOOK_INIT(fwrite);

  return _libc_fwrite(buf, size, num, stream);
}
SBMA_EXPORT(internal, size_t
libc_fwrite(void const * const buf, size_t const size, size_t const num,
            FILE * const stream));


/*************************************************************************/
/*! Hook: libc mlock */
/*************************************************************************/
SBMA_EXTERN int
libc_mlock(void const * const addr, size_t const len)
{
  static int (*_libc_mlock)(void const*, size_t)=NULL;

  HOOK_INIT(mlock);

  return _libc_mlock(addr, len);
}
SBMA_EXPORT(internal, int
libc_mlock(void const * const addr, size_t const len));


/*************************************************************************/
/*! Hook: libc mlockall */
/*************************************************************************/
SBMA_EXTERN int
libc_mlockall(int flags)
{
  static int (*_libc_mlockall)(int)=NULL;

  HOOK_INIT(mlockall);

  return _libc_mlockall(flags);
}
SBMA_EXPORT(internal, int
libc_mlockall(int flags));


/****************************************************************************/
/*! Hook: libc msync */
/****************************************************************************/
SBMA_EXTERN int
libc_msync(void * const addr, size_t const len, int const flags)
{
  static int (*_libc_msync)(void*, size_t, int)=NULL;

  HOOK_INIT(msync);

  return _libc_msync(addr, len, flags);
}
SBMA_EXPORT(internal, int
libc_msync(void * const addr, size_t const len, int const flags));


/****************************************************************************/
/*! Hook: libc nanosleep */
/****************************************************************************/
SBMA_EXTERN int
libc_nanosleep(struct timespec const * const req, struct timespec * const rem)
{
  static int (*_libc_nanosleep)(struct timespec const*, struct timespec*)=NULL;

  HOOK_INIT(nanosleep);

  return _libc_nanosleep(req, rem);
}
SBMA_EXPORT(internal, int
libc_nanosleep(struct timespec const * const req,
               struct timespec * const rem));


/****************************************************************************/
/*! Hook: libc sem_wait */
/****************************************************************************/
SBMA_EXTERN int
libc_sem_wait(sem_t * const sem)
{
  static int (*_libc_sem_wait)(sem_t *)=NULL;

  HOOK_INIT(sem_wait);

  return _libc_sem_wait(sem);
}
SBMA_EXPORT(internal, int
libc_sem_wait(sem_t * const sem));


/****************************************************************************/
/*! Hook: libc sem_timedwait */
/****************************************************************************/
SBMA_EXTERN int
libc_sem_timedwait(sem_t * const sem, struct timespec const * const ts)
{
  static int (*_libc_sem_timedwait)(sem_t *, struct timespec const *)=NULL;

  HOOK_INIT(sem_timedwait);

  return _libc_sem_timedwait(sem, ts);
}
SBMA_EXPORT(internal, int
libc_sem_timedwait(sem_t * const sem, struct timespec const * const ts));


/****************************************************************************/
/*! Hook: libc mq_send */
/****************************************************************************/
SBMA_EXTERN int
libc_mq_send(mqd_t const mqdes, char const * const msg_ptr,
             size_t const msg_len, unsigned const msg_prio)
{
  static int (*_libc_mq_send)(mqd_t, char const *, size_t, unsigned)=NULL;

  HOOK_INIT(mq_send);

  return _libc_mq_send(mqdes, msg_ptr, msg_len, msg_prio);
}
SBMA_EXPORT(internal, int
libc_mq_send(mqd_t const mqdes, char const * const msg_ptr,
             size_t const msg_len, unsigned const msg_prio));


/****************************************************************************/
/*! Hook: libc mq_timedsend */
/****************************************************************************/
SBMA_EXTERN int
libc_mq_timedsend(mqd_t const mqdes, char const * const msg_ptr,
                  size_t const msg_len, unsigned const msg_prio,
                  struct timespec const * const abs_timeout)
{
  static int (*_libc_mq_timedsend)(mqd_t, char const *, size_t, unsigned,\
    struct timespec const *)=NULL;

  HOOK_INIT(mq_timedsend);

  return _libc_mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio, abs_timeout);
}
SBMA_EXPORT(internal, int
libc_mq_timedsend(mqd_t const mqdes, char const * const msg_ptr,
                  size_t const msg_len, unsigned const msg_prio,
                  struct timespec const * const abs_timeout));


/****************************************************************************/
/*! Hook: libc mq_receive */
/****************************************************************************/
SBMA_EXTERN ssize_t
libc_mq_receive(mqd_t const mqdes, char * const msg_ptr, size_t const msg_len,
                unsigned * const msg_prio)
{
  static int (*_libc_mq_receive)(mqd_t, char *, size_t, unsigned *)=NULL;

  HOOK_INIT(mq_receive);

  return _libc_mq_receive(mqdes, msg_ptr, msg_len, msg_prio);
}
SBMA_EXPORT(internal, ssize_t
libc_mq_receive(mqd_t const mqdes, char * const msg_ptr, size_t const msg_len,
                unsigned * const msg_prio));


/****************************************************************************/
/*! Hook: libc mq_timedreceive */
/****************************************************************************/
SBMA_EXTERN ssize_t
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
SBMA_EXPORT(internal, ssize_t
libc_mq_timedreceive(mqd_t const mqdes, char * const msg_ptr,
                     size_t const msg_len, unsigned * const msg_prio,
                     struct timespec const * const abs_timeout));


/****************************************************************************/
/*! Hook: malloc */
/****************************************************************************/
SBMA_EXTERN void *
malloc(size_t const size)
{
  HOOK_INIT(calloc);

  if (VMM_OSVMM == (vmm.opts&VMM_OSVMM))
    return libc_malloc(size);
  else
    return SBMA_malloc(size);
}
SBMA_EXPORT(default, void *
malloc(size_t const size));


/*************************************************************************/
/*! Hook: calloc */
/*************************************************************************/
SBMA_EXTERN void *
calloc(size_t const num, size_t const size)
{
  HOOK_INIT(calloc);

  if (internal_calloc == _libc_calloc)
    return internal_calloc(num, size);

  if (VMM_OSVMM == (vmm.opts&VMM_OSVMM))
    return libc_calloc(num, size);
  else
    return SBMA_calloc(num, size);
}
SBMA_EXPORT(default, void *
calloc(size_t const num, size_t const size));


/*************************************************************************/
/*! Hook: realloc */
/*************************************************************************/
SBMA_EXTERN void *
realloc(void * const ptr, size_t const size)
{
  HOOK_INIT(calloc);

  if (NULL == ptr)
    return SBMA_malloc(size);

  if (VMM_OSVMM == (vmm.opts&VMM_OSVMM))
    return libc_realloc(ptr, size);
  else
    return SBMA_realloc(ptr, size);
}
SBMA_EXPORT(default, void *
realloc(void * const ptr, size_t const size));


/*************************************************************************/
/*! Hook: free */
/*************************************************************************/
SBMA_EXTERN void
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

  if (VMM_OSVMM == (vmm.opts&VMM_OSVMM))
    libc_free(ptr);
  else
    SBMA_free(ptr);
}
SBMA_EXPORT(default, void
free(void * const ptr));


/*************************************************************************/
/*! Hook: memcpy */
/*************************************************************************/
SBMA_EXTERN void *
memcpy(void * const dst, void const * const src, size_t const num)
{
  HOOK_INIT(calloc); /* Why is this here? */

  if (1 == SBMA_mexist(dst) && 1 == SBMA_mexist(src)) {
    (void)SBMA_mtouch_atomic(dst, num, src, num);
  }
  else {
    if (1 == SBMA_mexist(dst))
      (void)SBMA_mtouch(dst, num);
    if (1 == SBMA_mexist(src))
      (void)SBMA_mtouch((void*)src, num);
  }

  return libc_memcpy(dst, src, num);
}
SBMA_EXPORT(default, void *
memcpy(void * const dst, void const * const src, size_t const num));


/*************************************************************************/
/*! Hook: memmove */
/*************************************************************************/
SBMA_EXTERN void *
memmove(void * const dst, void const * const src, size_t const num)
{
  HOOK_INIT(calloc); /* Why is this here? */

  if (1 == SBMA_mexist(dst) && 1 == SBMA_mexist(src)) {
    (void)SBMA_mtouch_atomic(dst, num, src, num);
  }
  else {
    if (1 == SBMA_mexist(dst))
      (void)SBMA_mtouch(dst, num);
    if (1 == SBMA_mexist(src))
      (void)SBMA_mtouch((void*)src, num);
  }

  return libc_memmove(dst, src, num);
}
SBMA_EXPORT(default, void *
memmove(void * const dst, void const * const src, size_t const num));


/*************************************************************************/
/*! Hook: mallinfo */
/*************************************************************************/
SBMA_EXTERN struct mallinfo
mallinfo(void)
{
  HOOK_INIT(calloc); /* why is this here? */

  return SBMA_mallinfo();
}
SBMA_EXPORT(default, struct mallinfo
mallinfo(void));


/*************************************************************************/
/*! Hook: stat */
/*************************************************************************/
SBMA_EXTERN int
stat(char const * path, struct stat * buf)
{
  if (1 == SBMA_mexist(path))
    (void)SBMA_mtouch((void*)path, strlen(path));
  if (1 == SBMA_mexist(buf))
    (void)SBMA_mtouch((void*)buf, sizeof(struct stat));

  return libc_stat(path, buf);
}
SBMA_EXPORT(default, int
stat(char const * path, struct stat * buf));


/*************************************************************************/
/*! Hook: __xstat */
/*************************************************************************/
SBMA_EXTERN int
__xstat(int ver, const char * path, struct stat * buf)
{
  if (1 == SBMA_mexist(path))
    (void)SBMA_mtouch((void*)path, strlen(path));
  if (1 == SBMA_mexist(buf))
    (void)SBMA_mtouch((void*)buf, sizeof(struct stat));

  return libc___xstat(ver, path, buf);
}
SBMA_EXPORT(default, int
__xstat(int ver, const char * path, struct stat * buf));


/*************************************************************************/
/*! Hook: __xstat64 */
/*************************************************************************/
#if 0
#ifdef __USE_LARGEFILE64
SBMA_EXTERN int
__xstat64(int ver, const char * path, struct stat64 * buf)
{
  if (1 == SBMA_mexist(path))
    (void)SBMA_mtouch((void*)path, strlen(path));

  return libc_xstat64(ver, path, buf);
}
SBMA_EXPORT(default, int
__xstat64(int ver, const char * path, struct stat64 * buf));
#endif
#endif


/*************************************************************************/
/*! Hook: stat */
/*************************************************************************/
SBMA_EXTERN int
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
SBMA_EXPORT(default, int
open(char const * path, int flags, ...));


/*************************************************************************/
/*! Hook: read */
/*************************************************************************/
SBMA_EXTERN ssize_t
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
SBMA_EXPORT(default, ssize_t
read(int const fd, void * const buf, size_t const count));


/*************************************************************************/
/*! Hook: write */
/*************************************************************************/
SBMA_EXTERN ssize_t
write(int const fd, void const * const buf, size_t const count)
{
  if (1 == SBMA_mexist(buf))
    (void)SBMA_mtouch((void*)buf, count);

  return libc_write(fd, buf, count);
}
SBMA_EXPORT(default, ssize_t
write(int const fd, void const * const buf, size_t const count));


/*************************************************************************/
/*! Hook: fread */
/*************************************************************************/
SBMA_EXTERN size_t
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
SBMA_EXPORT(default, size_t
fread(void * const buf, size_t const size, size_t const num,
      FILE * const stream));


/*************************************************************************/
/*! Hook: fwrite */
/*************************************************************************/
SBMA_EXTERN size_t
fwrite(void const * const buf, size_t const size, size_t const num,
       FILE * const stream)
{
  if (1 == SBMA_mexist(buf))
    (void)SBMA_mtouch((void*)buf, size);

  return libc_fwrite(buf, size, num, stream);
}
SBMA_EXPORT(default, size_t
fwrite(void const * const buf, size_t const size, size_t const num,
       FILE * const stream));


/*************************************************************************/
/*! Hook: mlock */
/*************************************************************************/
SBMA_EXTERN int
mlock(void const * const addr, size_t const len)
{
  (void)SBMA_mtouch((void*)addr, len);
  return libc_mlock(addr, len);
}
SBMA_EXPORT(default, int
mlock(void const * const addr, size_t const len));


/*************************************************************************/
/*! Hook: mlockall */
/*************************************************************************/
SBMA_EXTERN int
mlockall(int flags)
{
  (void)SBMA_mtouchall();
  return libc_mlockall(flags);
}
SBMA_EXPORT(default, int
mlockall(int flags));


/****************************************************************************/
/*! Hook: msync */
/****************************************************************************/
SBMA_EXTERN int
msync(void * const addr, size_t const len, int const flags)
{
  if (0 == SBMA_mexist(addr))
    return libc_msync(addr, len, flags);
  else
    return SBMA_mevict(addr, len);
}
SBMA_EXPORT(default, int
msync(void * const addr, size_t const len, int const flags));


/****************************************************************************/
/*! Hook: nanosleep */
/****************************************************************************/
SBMA_EXTERN int
nanosleep(struct timespec const * const req, struct timespec * const rem)
{
  int ret;

  DEADLOCK_ALARM_ON();

  /* transition to blocked state */
  ret = __ipc_block(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  /* perform blocking wait */
  ret = libc_nanosleep(req, rem);
  if (-1 == ret) {
    (void)__ipc_unblock(&(vmm.ipc));
    return -1;
  }

  /* transition to running state */
  ret = __ipc_unblock(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  DEADLOCK_ALARM_OFF();

  return 0;
}
SBMA_EXPORT(default, int
nanosleep(struct timespec const * const req, struct timespec * const rem));


/****************************************************************************/
/*! Hook: sem_wait */
/****************************************************************************/
SBMA_EXTERN int
sem_wait(sem_t * const sem)
{
  int ret;

  DEADLOCK_ALARM_ON();

  /* transition to blocked state */
  ret = __ipc_block(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  /* NOTE: If a signal is received here or at 'marker', then it will be
   * identified when __ipc_unblock is called and errno will be set to EAGAIN
   * accordingly. Note that in the case that errno is set to EAGAIN, -1 will
   * not be returned, but rather a value indicating success. */

  /* perform blocking wait */
  ret = libc_sem_wait(sem);
  if (-1 == ret) {
    if (EINTR == errno) {
      (void)__ipc_unblock(&(vmm.ipc));
      errno = EINTR;
    }
    else {
      (void)__ipc_unblock(&(vmm.ipc));
    }
    return -1;
  }

  /* NOTE: marker */

  /* transition to running state */
  ret = __ipc_unblock(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  DEADLOCK_ALARM_OFF();

  return 0;
}
SBMA_EXPORT(default, int
sem_wait(sem_t * const sem));


/****************************************************************************/
/*! Hook: sem_timedwait */
/****************************************************************************/
SBMA_EXTERN int
sem_timedwait(sem_t * const sem, struct timespec const * const ts)
{
  int ret;

  DEADLOCK_ALARM_ON();

  /* transition to blocked state */
  ret = __ipc_block(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  /* perform blocking wait */
  ret = libc_sem_timedwait(sem, ts);
  if (-1 == ret) {
    if (EINTR == errno) {
      (void)__ipc_unblock(&(vmm.ipc));
      errno = EINTR;
    }
    else {
      (void)__ipc_unblock(&(vmm.ipc));
    }
    return -1;
  }

  /* transition to running state */
  ret = __ipc_unblock(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  DEADLOCK_ALARM_OFF();

  return 0;
}
SBMA_EXPORT(default, int
sem_timedwait(sem_t * const sem, struct timespec const * const ts));


/****************************************************************************/
/*! Hook: mq_send */
/****************************************************************************/
SBMA_EXTERN int
mq_send(mqd_t const mqdes, char const * const msg_ptr, size_t const msg_len,
        unsigned const msg_prio)
{
  int ret;

  DEADLOCK_ALARM_ON();

  /* transition to blocked state */
  ret = __ipc_block(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  /* perform blocking send */
  ret = libc_mq_send(mqdes, msg_ptr, msg_len, msg_prio);
  if (-1 == ret) {
    if (EINTR == errno) {
      (void)__ipc_unblock(&(vmm.ipc));
      errno = EINTR;
    }
    else {
      (void)__ipc_unblock(&(vmm.ipc));
    }
    return -1;
  }

  /* transition to running state */
  ret = __ipc_unblock(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  DEADLOCK_ALARM_OFF();

  return 0;
}
SBMA_EXPORT(default, int
mq_send(mqd_t const mqdes, char const * const msg_ptr, size_t const msg_len,
        unsigned const msg_prio));


/****************************************************************************/
/*! Hook: mq_timedsend */
/****************************************************************************/
SBMA_EXTERN int
mq_timedsend(mqd_t const mqdes, char const * const msg_ptr,
             size_t const msg_len, unsigned const msg_prio,
             struct timespec const * const abs_timeout)
{
  int ret;

  DEADLOCK_ALARM_ON();

  /* transition to blocked state */
  ret = __ipc_block(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  /* perform block send */
  ret = libc_mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio, abs_timeout);
  if (-1 == ret) {
    if (EINTR == errno) {
      (void)__ipc_unblock(&(vmm.ipc));
      errno = EINTR;
    }
    else {
      (void)__ipc_unblock(&(vmm.ipc));
    }
    return -1;
  }

  /* transition to running state */
  ret = __ipc_unblock(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  DEADLOCK_ALARM_OFF();

  return 0;
}
SBMA_EXPORT(default, int
mq_timedsend(mqd_t const mqdes, char const * const msg_ptr,
             size_t const msg_len, unsigned const msg_prio,
             struct timespec const * const abs_timeout));


/****************************************************************************/
/*! Hook: mq_receive */
/****************************************************************************/
SBMA_EXTERN ssize_t
mq_receive(mqd_t const mqdes, char * const msg_ptr, size_t const msg_len,
           unsigned * const msg_prio)
{
  int ret;
  ssize_t retval;

  DEADLOCK_ALARM_ON();

  /* transition to blocked state */
  ret = __ipc_block(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  /* perform blocking receive */
  retval = libc_mq_receive(mqdes, msg_ptr, msg_len, msg_prio);
  if (-1 == retval) {
    if (EINTR == errno) {
      (void)__ipc_unblock(&(vmm.ipc));
      errno = EINTR;
    }
    else {
      (void)__ipc_unblock(&(vmm.ipc));
    }
    return -1;
  }

  /* transition to running state */
  ret = __ipc_unblock(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  DEADLOCK_ALARM_OFF();

  return retval;
}
SBMA_EXPORT(default, ssize_t
mq_receive(mqd_t const mqdes, char * const msg_ptr, size_t const msg_len,
           unsigned * const msg_prio));


/****************************************************************************/
/*! Hook: mq_timedreceive */
/****************************************************************************/
SBMA_EXTERN ssize_t
mq_timedreceive(mqd_t const mqdes, char * const msg_ptr, size_t const msg_len,
                unsigned * const msg_prio,
                struct timespec const * const abs_timeout)
{
  int ret;
  ssize_t retval;

  DEADLOCK_ALARM_ON();

  /* transition to blocked state */
  ret = __ipc_block(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  /* perform blocking receive */
  retval = libc_mq_timedreceive(mqdes, msg_ptr, msg_len, msg_prio,\
    abs_timeout);
  if (-1 == retval) {
    if (EINTR == errno) {
      (void)__ipc_unblock(&(vmm.ipc));
      errno = EINTR;
    }
    else {
      (void)__ipc_unblock(&(vmm.ipc));
    }
    return -1;
  }

  /* transition to running state */
  ret = __ipc_unblock(&(vmm.ipc));
  if (-1 == ret)
    return -1;

  DEADLOCK_ALARM_OFF();

  return retval;
}
SBMA_EXPORT(default, ssize_t
mq_timedreceive(mqd_t const mqdes, char * const msg_ptr, size_t const msg_len,
                unsigned * const msg_prio,
                struct timespec const * const abs_timeout));


#pragma GCC pop_options
