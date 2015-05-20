#define _GNU_SOURCE

#include <assert.h>   /* assert */
#include <omp.h>      /* openmp library */
#include <stdio.h>    /* printf */
#include <stdlib.h>   /* EXIT_SUCCESS */
#include <string.h>   /* memset */
#include <sys/mman.h> /* mmap, mremap, munmap */
#include <unistd.h>   /* sysconf */

int main()
{
  int ret;
  size_t i, len, pagesize;
  char * addr;

  pagesize = sysconf(_SC_PAGESIZE);
  len = 4*pagesize;

  /* mmap a persistent memory region. */
  addr = mmap(NULL, len, PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,
    -1, 0);
  assert(MAP_FAILED != addr);

  /* initialize memory region with invalid values. */
  memset(addr, ~0, len);

  /* remove all access privileges from persistent memory region. */
  ret = mprotect(addr, len, PROT_NONE);
  assert(-1 != ret);

#pragma omp parallel num_threads(4) default(none) shared(addr,pagesize) \
  private(ret)
{
  int tid;
  char * taddr;

  tid = omp_get_thread_num();

  /* mmap a page into a temporary address with write privileges. */
  taddr = mmap(NULL, pagesize, PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  assert(MAP_FAILED != taddr);

  /* fill temporary page. */
  memset(taddr, tid, pagesize);

  /* remove write privileges from temporary page and grant read-only
   * privileges. */
  ret = mprotect(taddr, pagesize, PROT_READ);
  assert(-1 != ret);

  /* mremap temporary page to the correct location in persistent memory
   * region. */
  taddr = mremap(taddr, pagesize, pagesize, MREMAP_MAYMOVE|MREMAP_FIXED,
    addr+tid*pagesize);
  assert(MAP_FAILED != taddr);
}

  /* validate results. */
  for (i=0; i<len; ++i)
    assert((char)(i/pagesize) == addr[i]);

  /* coalesce adjacent mappings (unnecessary). */
  //addr = mremap(addr, len, len, 0);
  //assert(MAP_FAILED != addr);

  /* unmap persistent memory region. */
  munmap(addr, len);

  return EXIT_SUCCESS;
}
