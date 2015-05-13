#include "impl.h"

#if defined (USE_LOAD)

static char const * _tmp_file;

extern void
io_init(char const * const __tmp_file)
{
  _tmp_file = __tmp_file;
}

extern void
io_read(void * const addr, size_t const off, size_t size)
{
  int fd;
  ssize_t ret;
  char * buf;
  void * tmp_addr;

#if !defined(USE_GHOST)
  tmp_addr = (void*)addr;
# if !defined(USE_LIBC)
  ret = mprotect(tmp_addr, size, PROT_WRITE);
  assert(-1 != ret);
# endif
#else
  tmp_addr = mmap(NULL, size, PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  assert(MAP_FAILED != tmp_addr);
#endif

  fd = open(TMPFILE, O_RDONLY);
  assert(-1 != fd);

  ret = lseek(fd, off, SEEK_SET);
  assert(-1 != ret);

  buf = (char*)tmp_addr;
  do {
    ret = read(fd, buf, size);
    assert(-1 != ret);

    buf  += ret;
    size -= ret;
  } while (size > 0);

  ret = close(fd);
  assert(-1 != ret);

#if !defined(USE_LIBC)
  ret = mprotect(tmp_addr, size, PROT_READ);
  assert(-1 != ret);
#endif

#if defined(USE_GHOST)
  tmp_addr = mremap(tmp_addr, size, size, MREMAP_MAYMOVE|MREMAP_FIXED, addr);
  assert(MAP_FAILED != tmp_addr);
#endif
}

extern void
io_write(void const * const addr, size_t size)
{
  int fd;
  ssize_t ret;
  char const * buf;

  fd = open(TMPFILE, O_WRONLY);
  assert(-1 != fd);

  buf = (char const*)addr;
  do {
    ret = write(fd, buf, size);
    assert(-1 != ret);

    buf  += ret;
    size -= ret;
  } while (size > 0);

  ret = close(fd);
  assert(-1 != ret);
}

#endif
