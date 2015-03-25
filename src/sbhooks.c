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
/* need to provide temporary calloc function for dlsym */
/****************************************************************************/
#define HOOK_INIT(func)                                                     \
do {                                                                        \
  if (NULL == libc_##func)                                                  \
    *((void **) &libc_##func) = dlsym(RTLD_NEXT, #func);                    \
} while (0)


/*************************************************************************/
/*! Hook: malloc */
/*************************************************************************/
extern void *
malloc(size_t const len)
{
  return SB_malloc(len);
}


/*************************************************************************/
/*! Hook: calloc */
/*************************************************************************/
extern void *
calloc(size_t const num, size_t const size)
{
  return SB_calloc(num, size);
}


/*************************************************************************/
/*! Hook: realloc */
/*************************************************************************/
extern void *
realloc(void * const ptr, size_t const len)
{
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
  if (NULL == ptr)
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
  static ssize_t (*libc_read)(int, void*, size_t)=NULL;

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
  static ssize_t (*libc_write)(int, void const*, size_t)=NULL;

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
  static size_t (*libc_fread)(void*, size_t, size_t, FILE *)=NULL;

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
  static size_t (*libc_fwrite)(void const*, size_t, size_t, FILE *)=NULL;

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
  static int (*libc_mlock)(void const*, size_t)=NULL;

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
  static int (*libc_munlock)(void const*, size_t)=NULL;

  HOOK_INIT(munlock);

  return libc_munlock(addr, len);
}


/*************************************************************************/
/*! Hook: mlockall */
/*************************************************************************/
extern int
mlockall(int flags)
{
  static int (*libc_mlockall)(int)=NULL;

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
  static int (*libc_munlockall)(void)=NULL;

  HOOK_INIT(munlockall);

  return libc_munlockall();
}


/****************************************************************************/
/*! Hook: msync */
/****************************************************************************/
extern int
msync(void * const addr, size_t const len, int const flags)
{
  static int (*libc_msync)(void*, size_t, int)=NULL;

  HOOK_INIT(msync);

  if (0 == SB_exists(addr))
    return libc_msync(addr, len, flags);
  else
    return SB_sync(addr, len);
}
