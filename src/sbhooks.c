#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <dlfcn.h>  /* dlsym */
#include <stddef.h> /* size_t */
#include <stdint.h> /* SIZE_MAX */
#include <stdio.h>  /* FILE */
#include <string.h> /* memset */
#include <unistd.h> /* ssize_t */

#include "sbmalloc.h"


/****************************************************************************/
/* hooks to standard c library functions */
/****************************************************************************/
static void *(*libc_malloc)(size_t) = NULL;
static void *(*libc_calloc)(size_t, size_t) = NULL;
static void *(*libc_realloc)(void*, size_t) = NULL;
static void (*libc_free)(void*) = NULL;
static ssize_t (*libc_read)(int, void*, size_t) = NULL;
static ssize_t (*libc_write)(int, void const*, size_t) = NULL;
static size_t (*libc_fread)(void*, size_t, size_t, FILE *) = NULL;
static size_t (*libc_fwrite)(void const*, size_t, size_t, FILE *) = NULL;
static int (*libc_mlock)(void const*, size_t) = NULL;
static int (*libc_munlock)(void const*, size_t) = NULL;
static int (*libc_mlockall)(int) = NULL;
static int (*libc_munlockall)(void) = NULL;
static int (*libc_msync)(void*, size_t, int) = NULL;


/****************************************************************************/
/* memory for __calloc */
/****************************************************************************/
static char __calloc_mem[1024*1024]={0};
static char * __calloc_ptr=__calloc_mem;


/****************************************************************************/
/* need to provide temporary calloc function for dlsym */
/****************************************************************************/
#define HOOK_INIT(func)                                                     \
do {                                                                        \
  if (NULL == libc_calloc)                                                  \
      libc_calloc = &__calloc;                                              \
  if (NULL == libc_##func)                                                  \
    *((void **) &libc_##func) = dlsym(RTLD_NEXT, #func);                    \
} while (0)


/****************************************************************************/
/*! Temporary calloc function to be used only for dlsym.  After the
 *  libc_calloc is populated using dlsym, this function will never be called
 *  again. */
/****************************************************************************/
static void *
__calloc(size_t const num, size_t const size)
{
  char * nptr=NULL;

  if (__calloc_ptr+(num*size) <= __calloc_mem+(1024*1024)) {
    nptr = __calloc_ptr;
    __calloc_ptr += (num*size);
  }

  return nptr;
}


/*************************************************************************/
/*! Hook: malloc */
/*************************************************************************/
extern void *
malloc(size_t const len)
{
  HOOK_INIT(malloc);

  return SB_malloc(len);
}


/*************************************************************************/
/*! Hook: calloc */
/*************************************************************************/
extern void *
calloc(size_t const num, size_t const size)
{
  HOOK_INIT(calloc);

  if (&__calloc == libc_calloc)
    return libc_calloc(num, size);

  return SB_calloc(num, size);
}


/*************************************************************************/
/*! Hook: realloc */
/*************************************************************************/
extern void *
realloc(void * const ptr, size_t const len)
{
  HOOK_INIT(realloc);

  if (NULL == ptr)
    return malloc(len);

  return SB_realloc(ptr, len);
}


/*************************************************************************/
/*! Hook: free */
/*************************************************************************/
extern void
free(void * const ptr)
{
  HOOK_INIT(free);

  if (NULL == ptr)
    return;

  /* skip __calloc allocations */
  if ((char*)ptr >= __calloc_mem && (char*)ptr < __calloc_mem+1024*1024)
    return;

  SB_free(ptr);
}


/*************************************************************************/
/*! Hook: malloc_stats */
/*************************************************************************/
extern void
malloc_stats(void)
{
  SB_malloc_stats();
}


/*************************************************************************/
/*! Hook: read */
/*************************************************************************/
extern ssize_t
read(int const fd, void * const buf, size_t const count)
{
  HOOK_INIT(read);

  /* SB_load(buf, count, SBPAGE_DIRTY should work here, but it is not */
  if (1 == SB_exists(buf))
    memset(buf, 0, count);

  return libc_read(fd, buf, count);
}


/*************************************************************************/
/*! Hook: write */
/*************************************************************************/
extern ssize_t
write(int const fd, void const * const buf, size_t const count)
{
  HOOK_INIT(write);

  SB_load(buf, count, SBPAGE_SYNC);

  return libc_write(fd, buf, count);
}


/*************************************************************************/
/*! Hook: fread */
/*************************************************************************/
extern size_t
fread(void * const buf, size_t const size, size_t const num,
      FILE * const stream)
{
  HOOK_INIT(fread);

  (void)SB_load(buf, size*num, SBPAGE_DIRTY);

  return libc_fread(buf, size, num, stream);
}


/*************************************************************************/
/*! Hook: fwrite */
/*************************************************************************/
extern size_t
fwrite(void const * const buf, size_t const size, size_t const num,
       FILE * const stream)
{
  HOOK_INIT(fwrite);

  (void)SB_load(buf, SIZE_MAX, SBPAGE_SYNC);

  return libc_fwrite(buf, size, num, stream);
}


/*************************************************************************/
/*! Hook: mlock */
/*************************************************************************/
extern int
mlock(void const * const addr, size_t const len)
{
  HOOK_INIT(mlock);

  (void)SB_load(addr, SIZE_MAX, SBPAGE_SYNC);

  return libc_mlock(addr, len);
}


/*************************************************************************/
/*! Hook: munlock */
/*************************************************************************/
extern int
munlock(void const * const addr, size_t const len)
{
  HOOK_INIT(munlock);

  return libc_munlock(addr, len);
}


/*************************************************************************/
/*! Hook: mlockall */
/*************************************************************************/
extern int
mlockall(int flags)
{
  HOOK_INIT(mlockall);

  SB_loadall(SBPAGE_SYNC);

  return libc_mlockall(flags);
}


/*************************************************************************/
/*! Hook: munlockall */
/*************************************************************************/
extern int
munlockall(void)
{
  HOOK_INIT(munlockall);

  return libc_munlockall();
}


/****************************************************************************/
/*! Hook: msync */
/****************************************************************************/
extern int
msync(void * const addr, size_t const len, int const flags)
{
  HOOK_INIT(msync);

  if (0 == SB_exists(addr))
    return libc_msync(addr, len, flags);
  else
    return SB_sync(addr, len);
}
