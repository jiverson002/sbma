#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#ifdef NDEBUG
# undef NDEBUG
#endif

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <time.h>
#include <unistd.h>


/* ============================ BEG CONFIG ================================ */
//#define USE_LIBC
//#define USE_LOAD
//#define USE_LAZY
//#define USE_GHOST
//#define USE_CTX
//#define USE_RAND
#define USE_RD
#define USE_WR
#define USE_RW

#if defined(USE_LIBC)
# if defined(USE_CTX)
#   undef USE_CTX
# endif
#endif
#if !defined(USE_LOAD)
# if defined(USE_GHOST)
#   undef USE_GHOST
# endif
# if defined(USE_CTX)
#   undef USE_CTX
# endif
#endif

//static size_t NUM_MEM = (1lu<<32)-(1lu<<30); /* 3.0GiB */
static size_t NUM_MEM = (1lu<<31)-(1lu<<29); /* 1.5GiB */
static size_t NUM_SYS = 1;                   /* 4KiB */
static char * TMPFILE = "/scratch/micro1";

typedef unsigned type_t;
/* ============================ END CONFIG ================================ */


static char * pflags=NULL;
static uintptr_t base=0;
static size_t page=0;
static size_t faults=0;
#define SYNC   1
#define DIRTY  2
#define ONDISK 4

#pragma GCC push_options
#pragma GCC optimize("-O0")
static void
_membarrier(void)
{
  __asm volatile("": : :"memory"); \
  __sync_synchronize();
}
#pragma GCC pop_options

#pragma GCC push_options
#pragma GCC optimize("-O0")
static void
_cacheflush(void)
{
  /* 1<<28 == 256MiB */
  long unsigned i;
  char * ptr = mmap(NULL, 1<<28, PROT_READ|PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  if (MAP_FAILED != ptr) {
    for (i=0; i<1<<28; ++i)
      ptr[i] = (char)i;
    for (i=0; i<1<<28; ++i)
      assert((char)i == ptr[i]);
    munmap(ptr, 1<<28);
  }
}
#pragma GCC pop_options

static inline void
_gettime(struct timespec * const t)
{
  struct timespec tt;
  clock_gettime(CLOCK_MONOTONIC, &tt);
  t->tv_sec = tt.tv_sec;
  t->tv_nsec = tt.tv_nsec;
}

static inline long unsigned
_getelapsed(struct timespec const * const ts,
            struct timespec const * const te)
{
  struct timespec t;
  if (te->tv_nsec < ts->tv_nsec) {
    t.tv_nsec = 1000000000UL + te->tv_nsec - ts->tv_nsec;
    t.tv_sec = te->tv_sec - 1 - ts->tv_sec;
  }
  else {
    t.tv_nsec = te->tv_nsec - ts->tv_nsec;
    t.tv_sec = te->tv_sec - ts->tv_sec;
  }
  return (unsigned long)(t.tv_sec * 1000000000UL + t.tv_nsec);
}

static inline void
_read(void * const addr, size_t const off, size_t size)
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

static inline void
_write(void const * const addr, size_t size)
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

static inline void
_segvhandler(int const sig, siginfo_t * const si, void * const ctx)
{
  int type;
  ssize_t ret;
  uintptr_t addr;
  size_t ip;

  assert(SIGSEGV == sig);

  ip   = ((uintptr_t)si->si_addr-base)/page;
  addr = base+ip*page;

#if !defined(USE_CTX)
  type = (SYNC != (pflags[ip]&SYNC)) ? SYNC : DIRTY;
#else
  type = (0x2 == (((ucontext_t*)ctx)->uc_mcontext.gregs[REG_ERR]&0x2)) ? DIRTY : SYNC;
#endif

  if (SYNC == type) {
    assert(SYNC != (pflags[ip]&SYNC));
    assert(DIRTY != (pflags[ip]&DIRTY));

#if defined(USE_LOAD)
    if (ONDISK == (pflags[ip]&ONDISK)) {
# if defined(USE_LAZY)
      _read((void*)addr, ip*page, page);
# else
      _read((void*)base, 0, NUM_MEM);
# endif
    }
    else {
#endif
#if defined(USE_LAZY)
      ret = mprotect((void*)addr, page, PROT_READ);
      assert(0 == ret);
      pflags[ip] = SYNC;
#else
      ret = mprotect((void*)addr, NUM_MEM, PROT_READ);
      assert(0 == ret);
      memset(pflags, SYNC, NUM_MEM/page);
#endif
#if defined(USE_LOAD)
    }
#endif
  }
  else {
#if !defined(USE_CTX)
    assert(SYNC == (pflags[ip]&SYNC));
#endif
    ret = mprotect((void*)addr, page, PROT_READ|PROT_WRITE);
    assert(0 == ret);
    pflags[ip] = DIRTY;
  }

  faults++;

#if !defined(USE_CTX)
  if (NULL == ctx) {} /* supress unused warning */
#endif
}

static inline void
_init(void)
{
  int ret;
  struct sigaction act;
  struct sigaction oldact;

  act.sa_flags     = SA_SIGINFO;
  act.sa_sigaction = _segvhandler;

  ret = sigemptyset(&(act.sa_mask));
  assert(0 == ret);
  ret = sigaction(SIGSEGV, &act, &oldact);
  assert(0 == ret);
}

static inline void
_parse(int argc, char * argv[])
{
  int i;

  for (i=1; i<argc; ++i) {
    if (0 == strncmp("--mem=", argv[i], 6)) {
      NUM_MEM = atol(argv[i]+6);
    }
    else if (0 == strncmp("--sys=", argv[i], 6)) {
      NUM_SYS = atol(argv[i]+6);
    }
    else if (0 == strncmp("--file=", argv[i], 7)) {
      TMPFILE = argv[i]+7;
    }
  }
}

int main(int argc, char * argv[])
{
#if defined(USE_LOAD)
  int fd;
#endif
  size_t i, j, ii, jj, kk;
  ssize_t ret;
  unsigned long t_rd, t_wr, t_rw;
  struct timespec ts, te;
  type_t tmp;
  type_t * addr, * rnum;

  _parse(argc, argv);

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "General ==================\n");
  fprintf(stderr, "==========================\n");
#if defined(USE_LIBC)
  fprintf(stderr, "  Library      =      libc\n");
#else
  fprintf(stderr, "  Library      =      sbma\n");
#endif
  fprintf(stderr, "  MiB I/O      = %9.0f\n", NUM_MEM/1000000.0);
  fprintf(stderr, "  SysPages I/O = %9lu\n", NUM_MEM/sysconf(_SC_PAGESIZE));
  fprintf(stderr, "  SysPage mult = %9lu\n", NUM_SYS);
  fprintf(stderr, "  Options      = ");
#if defined(USE_LOAD)
  fprintf(stderr, "load,");
#endif
#if defined(USE_LAZY)
  fprintf(stderr, "lazy,");
#endif
#if defined(USE_GHOST)
  fprintf(stderr, "ghost,");
#endif
#if defined(USE_CTX)
  fprintf(stderr, "context,");
#endif
  fprintf(stderr, "\n");
  fprintf(stderr, "  Temp file    = %s\n", TMPFILE);
  fprintf(stderr, "\n");

  page = sysconf(_SC_PAGESIZE)*NUM_SYS;
  assert(0 == (NUM_MEM&(page-1)));

  /* ===== Acquire resources ===== */
{
#if defined(USE_LIBC)
  addr = mmap(NULL, NUM_MEM, PROT_READ|PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  assert(MAP_FAILED != addr);
#else
  _init();

  addr = mmap(NULL, NUM_MEM, PROT_NONE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  assert(MAP_FAILED != addr);

  pflags = mmap(NULL, NUM_MEM/page, PROT_READ|PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  assert(MAP_FAILED != pflags);
#endif

  base = (uintptr_t)addr;
  assert(0 == (NUM_MEM&(page-1)));

  rnum = mmap(NULL, NUM_MEM, PROT_READ|PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  assert(MAP_FAILED != rnum);
}

  /* ===== Generate random array ===== */
{
  for (i=0; i<NUM_MEM/sizeof(type_t); ++i)
    rnum[i] = i;
  for (i=0; i<(NUM_MEM/sizeof(type_t)-1); ++i) {
    j = rand()%((NUM_MEM/sizeof(type_t)-1)-i)+i;
    tmp = rnum[j];
    rnum[j] = rnum[i];
    rnum[i] = tmp;
  }
}

  _cacheflush();

  /* ----- WRITE (new) ---- */
#if defined(USE_WR)
{
  _gettime(&ts);
#if defined(USE_RAND)
    ii = rnum[0];
#else
    ii = 0;
#endif
  addr[ii] = rnum[ii];
  for (i=1; i<NUM_MEM/sizeof(type_t); ++i) {
#if defined(USE_RAND)
    ii = rnum[i];
    jj = rnum[i-1];
#else
    ii = i;
    jj = i-1;
#endif
    addr[ii] = addr[jj]+rnum[ii];
  }
  _membarrier();
  _gettime(&te);
  t_wr = _getelapsed(&ts, &te);

#if defined(USE_RAND)
    ii = rnum[0];
#else
    ii = 0;
#endif
  assert(rnum[ii] == addr[ii]);
  for (i=1; i<NUM_MEM/sizeof(type_t); ++i) {
#if defined(USE_RAND)
    ii = rnum[i];
    jj = rnum[i-1];
#else
    ii = i;
    jj = i-1;
#endif
    assert(addr[jj]+rnum[ii] == addr[ii]);
  }

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Write (new) ==============\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s        = %9.0f\n", NUM_MEM/(t_wr/1000000.0));
  fprintf(stderr, "  SysPages/s   = %9.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_wr/1000000.0));
#if !defined(USE_LIBC)
  fprintf(stderr, "  # SIGSEGV    = %9zu\n", faults);
#endif
  fprintf(stderr, "\n");

#if !defined(USE_LIBC)
  memset(pflags, 0, NUM_MEM/page);
  ret = mprotect(addr, NUM_MEM, PROT_NONE);
  assert(0 == ret);
  faults = 0;
#endif

  _cacheflush();
}
#endif

  /* ----- WRITE (init) ---- */
#if defined(USE_WR)
{
  _gettime(&ts);
#if defined(USE_RAND)
    ii = rnum[0];
#else
    ii = 0;
#endif
  addr[ii] = rnum[ii];
  for (i=1; i<NUM_MEM/sizeof(type_t); ++i) {
#if defined(USE_RAND)
    ii = rnum[i];
    jj = rnum[i-1];
#else
    ii = i;
    jj = i-1;
#endif
    addr[ii] = addr[jj]+rnum[ii];
  }
  _membarrier();
  _gettime(&te);
  t_wr = _getelapsed(&ts, &te);

#if defined(USE_RAND)
    ii = rnum[0];
#else
    ii = 0;
#endif
  assert(rnum[ii] == addr[ii]);
  for (i=1; i<NUM_MEM/sizeof(type_t); ++i) {
#if defined(USE_RAND)
    ii = rnum[i];
    jj = rnum[i-1];
#else
    ii = i;
    jj = i-1;
#endif
    assert(addr[jj]+rnum[ii] == addr[ii]);
  }

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Write (init) =============\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s        = %9.0f\n", NUM_MEM/(t_wr/1000000.0));
  fprintf(stderr, "  SysPages/s   = %9.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_wr/1000000.0));
#if !defined(USE_LIBC)
  fprintf(stderr, "  # SIGSEGV    = %9zu\n", faults);
#endif
  fprintf(stderr, "\n");

#if !defined(USE_LIBC)
# if !defined(USE_LOAD)
  memset(pflags, 0, NUM_MEM/page);
# endif
  faults = 0;
#endif

  _cacheflush();
}
#endif

  /* ----- FILE SETUP ---- */
#if defined(USE_LOAD)
{
  fd = open(TMPFILE, O_RDWR|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);
  assert(-1 != fd);

  _write(addr, NUM_MEM);

  memset(addr, 0, NUM_MEM);

# if !defined(USE_LIBC)
  memset(pflags, ONDISK, NUM_MEM/page);
# endif

  _cacheflush();
}
#endif
#if !defined(USE_LIBC)
{
  ret = mprotect(addr, NUM_MEM, PROT_NONE);
  assert(0 == ret);

  _cacheflush();
}
#endif

  /* ----- READ ----- */
#if defined(USE_RD)
{
#if defined(USE_LOAD)
# if !defined(USE_LIBC)
  ret = madvise((void*)base, NUM_MEM, MADV_DONTNEED);
  assert(-1 != ret);
# endif
  ret = posix_fadvise(fd, 0, NUM_MEM, POSIX_FADV_DONTNEED);
  assert(-1 != ret);
#endif

  _gettime(&ts);
#if defined(USE_LIBC) && defined(USE_LOAD) && !defined(USE_LAZY)
  _read(addr, 0, NUM_MEM);
#endif
#if defined(USE_RAND)
  ii = rnum[0];
#else
  ii = 0;
#endif
#if defined(USE_LIBC) && defined(USE_LOAD) && defined(USE_LAZY)
  if (0 == ((uintptr_t)(addr+ii)&(page-1)))
    _read(addr+ii, ii*sizeof(type_t), page);
#endif
  assert(rnum[ii] == addr[ii]);
  for (i=1; i<NUM_MEM/sizeof(type_t); ++i) {
#if defined(USE_RAND)
    ii = rnum[i];
    jj = rnum[i-1];
#else
    ii = i;
    jj = i-1;
#endif
#if defined(USE_LIBC) && defined(USE_LOAD) && defined(USE_LAZY)
    if (0 == ((uintptr_t)(addr+ii)&(page-1)))
      _read(addr+ii, ii*sizeof(type_t), page);
#endif
    assert(addr[jj]+rnum[ii] == addr[ii]);
  }
  _membarrier();
  _gettime(&te);
  t_rd = _getelapsed(&ts, &te);

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Read =====================\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s        = %9.0f\n", NUM_MEM/(t_rd/1000000.0));
  fprintf(stderr, "  SysPages/s   = %9.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_rd/1000000.0));
#if !defined(USE_LIBC)
  fprintf(stderr, "  # SIGSEGV    = %9zu\n", faults);
#endif
  fprintf(stderr, "\n");

#if defined(USE_LOAD)
# if !defined(USE_LIBC)
  ret = mprotect(addr, NUM_MEM, PROT_WRITE);
  assert(0 == ret);
# endif
  memset(addr, 0, NUM_MEM);
#endif
#if !defined(USE_LIBC)
# if !defined(USE_LOAD)
  memset(pflags, 0, NUM_MEM/page);
# else
  memset(pflags, ONDISK, NUM_MEM/page);
# endif
  ret = mprotect(addr, NUM_MEM, PROT_NONE);
  assert(0 == ret);
  faults = 0;
#endif

  _cacheflush();
}
#endif

  /* ----- READ/WRITE ----- */
#if defined(USE_RW)
{
#if defined(USE_LOAD)
# if !defined(USE_LIBC)
  ret = madvise((void*)base, NUM_MEM, MADV_DONTNEED);
  assert(-1 != ret);
# endif
  ret = posix_fadvise(fd, 0, NUM_MEM, POSIX_FADV_DONTNEED);
  assert(-1 != ret);
#endif

  _gettime(&ts);
#if defined(USE_LIBC) && defined(USE_LOAD) && !defined(USE_LAZY)
  _read(addr, 0, NUM_MEM);
#endif
#if defined(USE_RAND)
  ii = rnum[0];
#else
  ii = 0;
#endif
#if defined(USE_LIBC) && defined(USE_LOAD) && defined(USE_LAZY)
  if (0 == ((uintptr_t)(addr+ii)&(page-1)))
    _read(addr+ii, ii*sizeof(type_t), page);
#endif
  addr[ii] += rnum[ii];
  for (i=1; i<NUM_MEM/sizeof(type_t); ++i) {
#if defined(USE_RAND)
    ii = rnum[i];
    jj = rnum[i-1];
#else
    ii = i;
    jj = i-1;
#endif
#if defined(USE_LIBC) && defined(USE_LOAD) && defined(USE_LAZY)
    if (0 == ((uintptr_t)(addr+ii)&(page-1)))
      _read(addr+ii, ii*sizeof(type_t), page);
#endif
    addr[ii] += addr[jj]+rnum[ii];
  }
  _membarrier();
  _gettime(&te);
  t_rw = _getelapsed(&ts, &te);

#if defined(USE_RAND)
  ii = rnum[1];
  jj = rnum[0];
#else
  ii = 1;
  jj = 0;
#endif
  addr[ii] -= addr[jj]+rnum[ii];
  addr[jj] -= rnum[jj];
  assert(rnum[jj] == addr[jj]);
  assert(addr[jj]+rnum[ii] == addr[ii]);
  addr[jj] += rnum[jj];
  addr[ii] += addr[jj]+rnum[ii];
  for (i=2; i<NUM_MEM/sizeof(type_t); ++i) {
#if defined(USE_RAND)
    ii = rnum[i];
    jj = rnum[i-1];
    kk = rnum[i-2];
#else
    ii = i;
    jj = i-1;
    kk = i-2;
#endif
    addr[ii] -= addr[jj]+rnum[ii];
    addr[jj] -= addr[kk]+rnum[jj];
    assert(addr[jj]+rnum[ii] == addr[ii]);
    addr[jj] += addr[kk]+rnum[jj];
    addr[ii] += addr[jj]+rnum[ii];
  }

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Read/Write ===============\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s        = %9.0f\n", NUM_MEM/(t_rw/1000000.0));
  fprintf(stderr, "  SysPages/s   = %9.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_rw/1000000.0));
#if !defined(USE_LIBC)
  fprintf(stderr, "  # SIGSEGV    = %9zu\n", faults);
#endif
}
#endif

  /* ===== Release resources ===== */
{
  ret = munmap(addr, NUM_MEM);
  assert(0 == ret);
  ret = munmap(rnum, NUM_MEM);
  assert(0 == ret);
#if !defined(USE_LIBC)
  ret = munmap(pflags, NUM_MEM/page);
  assert(0 == ret);
#endif
#if defined(USE_LOAD)
  ret = close(fd);
  assert(-1 != ret);
  ret = unlink(TMPFILE);
  assert(-1 != ret);
#endif
}

  return EXIT_SUCCESS;
}
