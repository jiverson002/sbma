#include "impl.h"

#if defined(USE_SBMA)

#define SYNC   1
#define DIRTY  2
#define ONDISK 4

static char * _pflags=NULL;
static uintptr_t _base=0;
static size_t _siz_pag=0;
static size_t _num_mem=0;
static size_t _num_sys=0;
static size_t _num_pag=0;
static size_t _faults=0;

static inline void
sbma_segvhandler(int const sig, siginfo_t * const si, void * const ctx)
{
  int type;
  ssize_t ret;
  uintptr_t addr;
  size_t ip;

  assert(SIGSEGV == sig);

  ip   = ((uintptr_t)si->si_addr-_base)/_siz_pag;
  addr = _base+ip*_siz_pag;

#if !defined(USE_CTX)
  type = (SYNC != (_pflags[ip]&SYNC)) ? SYNC : DIRTY;
#else
  type = (0x2 == (((ucontext_t*)ctx)->uc_mcontext.gregs[REG_ERR]&0x2)) ? DIRTY : SYNC;
#endif

  if (SYNC == type) {
    assert(SYNC != (_pflags[ip]&SYNC));
    assert(DIRTY != (_pflags[ip]&DIRTY));

#if defined(USE_LOAD)
    if (ONDISK == (_pflags[ip]&ONDISK)) {
# if defined(USE_LAZY)
      io_read((void*)addr, ip*_siz_pag, _siz_pag);
# else
      io_read((void*)_base, 0, _num_mem);
# endif
    }
    else {
#endif
#if defined(USE_LAZY)
      ret = mprotect((void*)addr, _siz_pag, PROT_READ);
      assert(0 == ret);
      _pflags[ip] = SYNC;
#else
      ret = mprotect((void*)addr, _num_mem, PROT_READ);
      assert(0 == ret);
      memset(_pflags, SYNC, _num_pag);
#endif
#if defined(USE_LOAD)
    }
#endif
  }
  else {
#if !defined(USE_CTX)
    assert(SYNC == (_pflags[ip]&SYNC));
#endif
    ret = mprotect((void*)addr, _siz_pag, PROT_READ|PROT_WRITE);
    assert(0 == ret);
    _pflags[ip] = DIRTY;
  }

  _faults++;

#if !defined(USE_CTX)
  if (NULL == ctx) {} /* supress unused warning */
#endif
}

extern char const *
impl_name(void)
{
  return "sbma";
}

extern void *
impl_init(size_t const __num_mem, size_t const __num_sys)
{
  int ret;
  struct sigaction act;
  struct sigaction oldact;

  act.sa_flags     = SA_SIGINFO;
  act.sa_sigaction = sbma_segvhandler;

  ret = sigemptyset(&(act.sa_mask));
  assert(0 == ret);
  ret = sigaction(SIGSEGV, &act, &oldact);
  assert(0 == ret);

  _siz_pag = sysconf(_SC_PAGESIZE)*__num_sys;
  _num_mem = __num_mem;
  _num_sys = __num_sys;
  _num_pag = __num_mem/_siz_pag;

  _pflags = mmap(NULL, _num_pag, PROT_READ|PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  assert(MAP_FAILED != _pflags);

  _base = (uintptr_t)mmap(NULL, _num_mem, PROT_NONE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  return (void*)_base;
}

extern void
impl_destroy(void)
{
  int ret;

  ret = munmap((void*)_base, _num_mem);
  assert(0 == ret);

  ret = munmap(_pflags, _num_pag);
  assert(0 == ret);
}

extern void
impl_flush(void)
{
  int ret;
  size_t i;

  /* remove all but ONDISK flag if it exists */
  for (i=0; i<_num_pag; ++i)
    _pflags[i] &= ONDISK;

#if defined(USE_LOAD)
# if 1
  ret = mprotect((void*)_base, _num_mem, PROT_WRITE);
  assert(0 == ret);

  memset((void*)_base, 0, _num_mem);
# else
  ret = madvise((void*)_base, _num_mem, MADV_DONTNEED);
  assert(-1 != ret);
# endif
#endif

  ret = mprotect((void*)_base, _num_mem, PROT_NONE);
  assert(0 == ret);

  _faults = 0;
}

extern void
impl_ondisk(void)
{
  memset(_pflags, ONDISK, _num_pag);
}

extern void
impl_fetch_bulk(void * const addr, size_t const off, size_t const size)
{
  if (NULL == addr || 0 == off || 0 == size) {}
}

extern void
impl_fetch_page(void * const addr, size_t const off, size_t const size)
{
  if (NULL == addr || 0 == off || 0 == size) {}
}

extern void
impl_aux_info(int const len1, int const len2)
{
  fprintf(stderr, "  %-*s = %*zu\n", len1, "# SIGSEGV", len2, _faults);
}

#endif
