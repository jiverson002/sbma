#include "impl.h"

#if defined (USE_LOAD)

static char const * _tmp_file=NULL;
static size_t _num_mem;

extern void
io_init(char const * const __tmp_file, size_t const __num_mem)
{
  _tmp_file = __tmp_file;
  _num_mem  = __num_mem;
}

extern void
io_destroy(void)
{
  int ret;
  ret = unlink(_tmp_file);
  assert(-1 != ret);
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

  fd = open(_tmp_file, O_RDONLY);
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

  fd = open(_tmp_file, O_WRONLY|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);
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

extern void
io_flush(void)
{
  int ret, fd;

  fd = open(_tmp_file, O_RDONLY);
  assert(-1 != fd);

  ret = posix_fadvise(fd, 0, _num_mem, POSIX_FADV_DONTNEED);
  assert(-1 != ret);

  ret = close(fd);
  assert(-1 != ret);
}

#endif
