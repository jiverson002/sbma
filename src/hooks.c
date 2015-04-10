#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

/****************************************************************************/
/* Need optimizations off or GCC will optimize away the termporary setting of
 * libc_calloc in the HOOK_INIT macro. */
/****************************************************************************/
#pragma GCC push_options
#pragma GCC optimize("O0")


#include <dlfcn.h>  /* dlsym */
#include <stddef.h> /* size_t */
#include <stdint.h> /* SIZE_MAX */
#include <stdio.h>  /* FILE */
#include <string.h> /* memset */
#include <unistd.h> /* ssize_t */

#include "klmalloc.h"
#include "sbmalloc.h"


/****************************************************************************/
/* need to provide temporary calloc function for dlsym */
/****************************************************************************/
#define HOOK_INIT(func)                                                     \
do {                                                                        \
  if (NULL == _libc_calloc) {                                               \
    _libc_calloc = internal_calloc;                                         \
    *((void **) &_libc_calloc) = dlsym(RTLD_NEXT, "calloc");                \
  }                                                                         \
  if (NULL == _libc_##func)                                                 \
    *((void **) &_libc_##func) = dlsym(RTLD_NEXT, #func);                   \
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
/*! Hook: malloc */
/*************************************************************************/
extern void *
malloc(size_t const size)
{
  HOOK_INIT(calloc);

  return KL_malloc(size);
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

  return KL_calloc(num, size);
}


/*************************************************************************/
/*! Hook: realloc */
/*************************************************************************/
extern void *
realloc(void * const ptr, size_t const size)
{
  HOOK_INIT(calloc);

  if (NULL == ptr)
    return malloc(size);

  return KL_realloc(ptr, size);
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

  KL_free(ptr);
}


/*************************************************************************/
/*! Hook: malloc_stats */
/*************************************************************************/
extern void
malloc_stats(void)
{
  HOOK_INIT(calloc);

  KL_malloc_stats();
}


/*************************************************************************/
/*! Hook: read */
/*************************************************************************/
extern ssize_t
read(int const fd, void * const buf, size_t const count)
{
  static ssize_t (*_libc_read)(int, void*, size_t)=NULL;

  HOOK_INIT(read);

  /* SB_load(buf, count, SBPAGE_DIRTY should work here, but it is not */
  if (1 == SB_exists(buf))
    memset(buf, 0, count);

  return _libc_read(fd, buf, count);
}


/*************************************************************************/
/*! Hook: write */
/*************************************************************************/
extern ssize_t
write(int const fd, void const * const buf, size_t const count)
{
  static ssize_t (*_libc_write)(int, void const*, size_t)=NULL;

  HOOK_INIT(write);

  SB_load(buf, count, SBPAGE_SYNC);

  return _libc_write(fd, buf, count);
}


/*************************************************************************/
/*! Hook: fread */
/*************************************************************************/
extern size_t
fread(void * const buf, size_t const size, size_t const num,
      FILE * const stream)
{
  static size_t (*_libc_fread)(void*, size_t, size_t, FILE *)=NULL;

  HOOK_INIT(fread);

  (void)SB_load(buf, size*num, SBPAGE_DIRTY);

  return _libc_fread(buf, size, num, stream);
}


/*************************************************************************/
/*! Hook: fwrite */
/*************************************************************************/
extern size_t
fwrite(void const * const buf, size_t const size, size_t const num,
       FILE * const stream)
{
  static size_t (*_libc_fwrite)(void const*, size_t, size_t, FILE *)=NULL;

  HOOK_INIT(fwrite);

  (void)SB_load(buf, SIZE_MAX, SBPAGE_SYNC);

  return _libc_fwrite(buf, size, num, stream);
}


/*************************************************************************/
/*! Hook: mlock */
/*************************************************************************/
extern int
mlock(void const * const addr, size_t const len)
{
  static int (*_libc_mlock)(void const*, size_t)=NULL;

  HOOK_INIT(mlock);

  (void)SB_load(addr, SIZE_MAX, SBPAGE_SYNC);

  return _libc_mlock(addr, len);
}


/*************************************************************************/
/*! Hook: munlock */
/*************************************************************************/
extern int
munlock(void const * const addr, size_t const len)
{
  static int (*_libc_munlock)(void const*, size_t)=NULL;

  HOOK_INIT(munlock);

  return _libc_munlock(addr, len);
}


/*************************************************************************/
/*! Hook: mlockall */
/*************************************************************************/
extern int
mlockall(int flags)
{
  static int (*_libc_mlockall)(int)=NULL;

  HOOK_INIT(mlockall);

  SB_loadall(SBPAGE_SYNC);

  return _libc_mlockall(flags);
}


/*************************************************************************/
/*! Hook: munlockall */
/*************************************************************************/
extern int
munlockall(void)
{
  static int (*_libc_munlockall)(void)=NULL;

  HOOK_INIT(munlockall);

  return _libc_munlockall();
}


/****************************************************************************/
/*! Hook: msync */
/****************************************************************************/
extern int
msync(void * const addr, size_t const len, int const flags)
{
  static int (*_libc_msync)(void*, size_t, int)=NULL;

  HOOK_INIT(msync);

  if (0 == SB_exists(addr))
    return _libc_msync(addr, len, flags);
  else
    return SB_sync(addr, len);
}


#pragma GCC pop_options
