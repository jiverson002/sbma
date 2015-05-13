#include "impl.h"

#if defined(USE_LIBC)

static size_t _num_mem=0;
static size_t _siz_pag=0;
static uintptr_t _base=0;

extern char const *
impl_name(void)
{
  return "libc";
}

extern void *
impl_init(size_t const __num_mem, size_t const __num_sys)
{
  _siz_pag = sysconf(_SC_PAGESIZE)*__num_sys;
  _num_mem = __num_mem;

  _base = (uintptr_t)mmap(NULL, _num_mem, PROT_READ|PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);

  return (void*)_base;
}

extern void
impl_destroy(void)
{
  int ret;

  ret = munmap((void*)_base, _num_mem);
  assert(0 == ret);
}

extern void
impl_flush(void)
{
# if defined(USE_LOAD)
#if 1
  memset((void*)_base, 0, _num_mem);
# else
  int ret;

  ret = madvise((void*)_base, _num_mem, MADV_DONTNEED);
  assert(-1 != ret);
# endif
#endif
}

extern void
impl_ondisk(void)
{
}

extern void
impl_fetch_bulk(void * const addr, size_t const off, size_t const size)
{
#if defined(USE_LOAD)
# if !defined(USE_LAZY)
  io_read(addr, off, size);
# endif
#endif
  if (NULL == addr || 0 == off || 0 == size) {}
}

extern void
impl_fetch_page(void * const addr, size_t const off, size_t const size)
{
#if defined(USE_LOAD)
# if defined(USE_LAZY)
  if (0 == ((uintptr_t)addr&(_siz_pag-1)))
    io_read(addr, off, size);
# endif
#endif
  if (NULL == addr || 0 == off || 0 == size) {}
}

#endif
