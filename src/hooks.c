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


#include <assert.h>    /* assert */
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

#include "klmalloc.h"
#include "sbma.h"


/****************************************************************************/
/* need to provide temporary calloc function for dlsym */
/****************************************************************************/
#define HOOK_INIT(func)                                                     \
do {                                                                        \
  if (NULL == _libc_calloc) {                                               \
    _libc_calloc = internal_calloc;                                         \
    *((void **) &_libc_calloc) = dlsym(RTLD_NEXT, "calloc");                \
    assert(NULL != _libc_calloc);                                           \
  }                                                                         \
  if (NULL == _libc_##func) {                                               \
    *((void **) &_libc_##func) = dlsym(RTLD_NEXT, #func);                   \
    assert(NULL != _libc_##func);                                           \
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


#include <stdint.h>
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
/*! Hook: libc close */
/*************************************************************************/
extern int
libc_close(int fd)
{
  static ssize_t (*_libc_close)(int)=NULL;

  HOOK_INIT(close);

  return _libc_close(fd);
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
/*! Hook: libc munlock */
/*************************************************************************/
extern int
libc_munlock(void const * const addr, size_t const len)
{
  static int (*_libc_munlock)(void const*, size_t)=NULL;

  HOOK_INIT(munlock);

  return _libc_munlock(addr, len);
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


/*************************************************************************/
/*! Hook: libc munlockall */
/*************************************************************************/
extern int
libc_munlockall(void)
{
  static int (*_libc_munlockall)(void)=NULL;

  HOOK_INIT(munlockall);

  return _libc_munlockall();
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
  void * ptr = KL_malloc(size);
#endif
  printf("m %p %zu\n", ptr, size);
  return ptr;*/

#ifdef USE_LIBC
  return libc_malloc(size);
#else
  return KL_malloc(size);
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
  return KL_calloc(num, size);
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
    return KL_malloc(size);

#ifdef USE_LIBC
  return libc_realloc(ptr, size);
#else
  return KL_realloc(ptr, size);
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
  KL_free(ptr);
#endif
}


/*************************************************************************/
/*! Hook: mallinfo */
/*************************************************************************/
extern struct mallinfo
mallinfo(void)
{
  HOOK_INIT(calloc);

  return KL_mallinfo();
}


/*************************************************************************/
/*! Hook: stat */
/*************************************************************************/
extern int
stat(char const * path, struct stat * buf)
{
  if (1 == sbma_mexist(path))
    (void)sbma_mtouch((void*)path, strlen(path));
  if (1 == sbma_mexist(buf))
    (void)sbma_mtouch((void*)buf, sizeof(struct stat));

  return libc_stat(path, buf);
}


/*************************************************************************/
/*! Hook: __xstat */
/*************************************************************************/
extern int
__xstat(int ver, const char * path, struct stat * buf)
{
  //printf("[%5d]:%s:%d\n", (int)getpid(), __func__, __LINE__);
  if (1 == sbma_mexist(path)) {
    //printf("[%5d]:%s:%d\n", (int)getpid(), __func__, __LINE__);
    (void)sbma_mtouch((void*)path, strlen(path));
  }
  //printf("[%5d]:%s:%d\n", (int)getpid(), __func__, __LINE__);
  if (1 == sbma_mexist(buf)) {
    //printf("[%5d]:%s:%d\n", (int)getpid(), __func__, __LINE__);
    (void)sbma_mtouch((void*)buf, sizeof(struct stat));
  }
  //printf("[%5d]:%s:%d\n", (int)getpid(), __func__, __LINE__);

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
  if (1 == sbma_mexist(path))
    (void)sbma_mtouch((void*)path, strlen(path));

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

  if (1 == sbma_mexist(path))
    (void)sbma_mtouch((void*)path, strlen(path));

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
  if (1 == sbma_mexist(buf)) {
    /* NOTE: memset() must be used instead of sbma_mtouch() for the following
     * reason. If the relevant memory page has been written to disk and thus,
     * given no R/W permissions, the using sbma_mtouch() with SBPAGE_DIRTY
     * will give the relevant page appropriate permissions, however, it will
     * cause the page not be read from disk.  This is incorrect if the page is
     * a shared page, since then any data that was in the shared page, but not
     * part of the relevant memory, will be lost. */
    //(void)sbma_mtouch(buf, count);
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
  //printf("[%5d]:%s:%d\n", (int)getpid(), __func__, __LINE__);
  if (1 == sbma_mexist(buf)) {
    //printf("[%5d]:%s:%d\n", (int)getpid(), __func__, __LINE__);
    (void)sbma_mtouch((void*)buf, count);
  }
  //printf("[%5d]:%s:%d %c\n", (int)getpid(), __func__, __LINE__,
    //((char*)buf)[0]);
  //for (size_t i=0; i<count; ++i) {
    //printf("%zu", i);
    //fflush(stdout);
    //if (((char*)buf)[i] == 10) {
      //printf("-");
      //fflush(stdout);
    //}
    //printf(".");
    //fflush(stdout);
  //}
  //printf("\n");
  //printf("[%5d]:%s:%d\n", (int)getpid(), __func__, __LINE__);

  return libc_write(fd, buf, count);
}


/*************************************************************************/
/*! Hook: fread */
/*************************************************************************/
extern size_t
fread(void * const buf, size_t const size, size_t const num,
      FILE * const stream)
{
  if (1 == sbma_mexist(buf)) {
    /* NOTE: For an explaination of why memset() must be used instead of
     * sbma_mtouch(), see discussion in read(). */
    //(void)sbma_mtouch(buf, size*num);
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
  if (1 == sbma_mexist(buf))
    (void)sbma_mtouch((void*)buf, size);

  return libc_fwrite(buf, size, num, stream);
}


/*************************************************************************/
/*! Hook: mlock */
/*************************************************************************/
extern int
mlock(void const * const addr, size_t const len)
{
  (void)sbma_mtouch((void*)addr, len);

  return libc_mlock(addr, len);
}


/*************************************************************************/
/*! Hook: munlock */
/*************************************************************************/
extern int
munlock(void const * const addr, size_t const len)
{
  return libc_munlock(addr, len);
}


/*************************************************************************/
/*! Hook: mlockall */
/*************************************************************************/
extern int
mlockall(int flags)
{
  (void)sbma_mtouchall();

  return libc_mlockall(flags);
}


/*************************************************************************/
/*! Hook: munlockall */
/*************************************************************************/
extern int
munlockall(void)
{
  return libc_munlockall();
}


/****************************************************************************/
/*! Hook: msync */
/****************************************************************************/
extern int
msync(void * const addr, size_t const len, int const flags)
{
  /*if (0 == sbma_mexist(addr))
    return libc_msync(addr, len, flags);
  else
    return sbma_sync(addr, len);*/
  if (NULL == addr || 0 == len || 0 == flags) {}
  return 0;
}


#pragma GCC pop_options
