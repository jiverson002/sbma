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
static int USE_LIBC   = 0;
static int USE_LOAD   = 0;
static int USE_LAZY   = 0;
static int USE_GHOST  = 0;
static int USE_CTX    = 0;

static size_t NUM_MEM = (1lu<<32)-(1lu<<30); /* 3.0GiB */
static size_t NUM_SYS = 1;                   /* 4KiB */
static char * TMPFILE = "/scratch/micro2";
/* ============================ END CONFIG ================================ */


static int filed=-1;
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
_cacheflush(void)
{
  /* 1<<28 == 256MiB */
  int ret;
  long unsigned i;
  char * ptr = mmap(NULL, 1<<28, PROT_READ|PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (MAP_FAILED != ptr) {
    for (i=0; i<1<<28; ++i)
      ptr[i] = (char)i;
    for (i=0; i<1<<28; ++i)
      assert((char)i == ptr[i]);
    munmap(ptr, 1<<28);
  }

  if (0 != base && 1 == USE_LOAD) {
    ret = madvise((void*)base, NUM_MEM, MADV_DONTNEED);
    assert(-1 != ret);
  }

  if (-1 != filed) {
    ret = posix_fadvise(filed, 0, NUM_MEM, POSIX_FADV_DONTNEED);
    assert(-1 != ret);
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

  if (0 == USE_GHOST) {
    tmp_addr = (void*)addr;

    if (0 == USE_LIBC) {
      ret = mprotect(tmp_addr, size, PROT_WRITE);
      assert(-1 != ret);
    }
  }
  else {
    tmp_addr = mmap(NULL, size, PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    assert(MAP_FAILED != tmp_addr);
  }

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

  if (0 == USE_LIBC) {
    ret = mprotect(tmp_addr, size, PROT_READ);
    assert(-1 != ret);
  }

  if (1 == USE_GHOST) {
    tmp_addr = mremap(tmp_addr, size, size, MREMAP_MAYMOVE|MREMAP_FIXED, addr);
    assert(MAP_FAILED != tmp_addr);
  }
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
  size_t len, off;
  ssize_t ret;
  uintptr_t addr, new;
  size_t ip;

  assert(SIGSEGV == sig);

  ip   = ((uintptr_t)si->si_addr-base)/page;
  addr = base+ip*page;

  if (0 == USE_CTX)
    type = (SYNC != (pflags[ip]&SYNC)) ? SYNC : DIRTY;
  else
    type = (0x2 == (((ucontext_t*)ctx)->uc_mcontext.gregs[REG_ERR]&0x2)) ? DIRTY : SYNC;

  if (SYNC == type) {
    assert(SYNC != (pflags[ip]&SYNC));
    assert(DIRTY != (pflags[ip]&DIRTY));

    if (1 == USE_LOAD && ONDISK == (pflags[ip]&ONDISK)) {
      /* ========================= BEG LOAD =============================== */
      if (1 == USE_LAZY) {
        len = page;
        off = ip*page;
        new = addr;
      }
      else {
        len = NUM_MEM;
        off = 0;
        new = base;
      }

      _read((void*)new, off, len);
      /* ========================= END LOAD =============================== */
    }
    else if (1 == USE_LAZY) {
      ret = mprotect((void*)addr, page, PROT_READ);
      assert(0 == ret);
    }
    else {
      ret = mprotect((void*)addr, NUM_MEM, PROT_READ);
      assert(0 == ret);
    }

    if (1 == USE_LAZY)
      pflags[ip] = SYNC;
    else if (0 == USE_LAZY)
      memset(pflags, SYNC, NUM_MEM/page);
  }
  else {
    if (0 == USE_CTX)
      assert(SYNC == (pflags[ip]&SYNC));

    ret = mprotect((void*)addr, page, PROT_READ|PROT_WRITE);
    assert(0 == ret);

    pflags[ip] = DIRTY;
  }

  faults++;
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
    if (0 == strncmp("--load", argv[i], 7)) {
      USE_LOAD = 1;
    }
    else if (0 == strncmp("--lazy", argv[i], 7)) {
      USE_LAZY = 1;
    }
    else if (0 == strncmp("--libc", argv[i], 7)) {
      USE_LIBC = 1;
    }
    else if (0 == strncmp("--ghost", argv[i], 8)) {
      USE_GHOST = 1;
    }
    else if (0 == strncmp("--context", argv[i], 10)) {
      fprintf(stderr, "--context does not work with optimizations level " \
        "above -O0\n");
      USE_CTX = 1;
    }
    else if (0 == strncmp("--mem=", argv[i], 6)) {
      NUM_MEM = atol(argv[i]+6);
    }
    else if (0 == strncmp("--sys=", argv[i], 6)) {
      NUM_SYS = atol(argv[i]+6);
    }
    else if (0 == strncmp("--file=", argv[i], 7)) {
      TMPFILE = argv[i]+7;
    }
  }

  if (1 == USE_LIBC)
    USE_CTX = 0;
  if (0 == USE_LOAD) {
    USE_LAZY  = 0;
    USE_GHOST = 0;
    USE_CTX   = 0;
  }
}

int main(int argc, char * argv[])
{
  ssize_t ret;
  unsigned long t_rd, t_wr, t_rw;
  size_t i;
  struct timespec ts, te;
  char * addr;

  _parse(argc, argv);

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "General ==================\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  Library      =      %s\n", 1==USE_LIBC?"libc":"sbma");
  fprintf(stderr, "  MiB I/O      = %9.0f\n", NUM_MEM/1000000.0);
  fprintf(stderr, "  SysPages I/O = %9lu\n", NUM_MEM/sysconf(_SC_PAGESIZE));
  fprintf(stderr, "  SysPage mult = %9lu\n", NUM_SYS);
  fprintf(stderr, "  Options      = %s%s%s%s\n", 1==USE_LOAD?"load,":"",
    1==USE_LAZY?"lazy,":"", 1==USE_GHOST?"ghost,":"",
    1==USE_CTX?"context,":"");
  fprintf(stderr, "  Temp file    = %s\n", TMPFILE);
  fprintf(stderr, "\n");

  page = sysconf(_SC_PAGESIZE)*NUM_SYS;
  assert(0 == (NUM_MEM&(page-1)));

  if (1 == USE_LIBC) {
    addr = mmap(NULL, NUM_MEM, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    assert(MAP_FAILED != addr);
  }
  else {
    _init();

    addr = mmap(NULL, NUM_MEM, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    assert(MAP_FAILED != addr);

    base = (uintptr_t)addr;
    assert(0 == (NUM_MEM&(page-1)));

    pflags = mmap(NULL, NUM_MEM/page, PROT_READ|PROT_WRITE,
      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    assert(MAP_FAILED != pflags);
  }

  _cacheflush();

  /* ===== Uninitialized memory tests ===== */
  /* ----- WRITE ---- */
  _gettime(&ts);
  for (i=0; i<NUM_MEM; ++i)
    addr[i] = (char)i;
  _gettime(&te);
  t_wr = _getelapsed(&ts, &te);

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Write (new) ==============\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s        = %9.0f\n", NUM_MEM/(t_wr/1000000.0));
  fprintf(stderr, "  SysPages/s   = %9.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_wr/1000000.0));
  if (0 == USE_LIBC)
    fprintf(stderr, "  # SIGSEGV    = %9zu\n", faults);
  fprintf(stderr, "\n");

  if (1 == USE_LOAD) {
    filed = open(TMPFILE, O_RDWR|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);
    assert(-1 != filed);

    _write(addr, NUM_MEM);

    memset(addr, 0, NUM_MEM);
  }
  if (0 == USE_LIBC) {
    if (0 == USE_LOAD)
      memset(pflags, 0, NUM_MEM/page);
    else
      memset(pflags, ONDISK, NUM_MEM/page);
    ret = mprotect(addr, NUM_MEM, PROT_NONE);
    assert(0 == ret);
    faults = 0;
  }
  _cacheflush();

  /* ----- READ ---- */
  _gettime(&ts);
  if (1 == USE_LIBC && 1 == USE_LOAD && 0 == USE_LAZY) {
    _read(addr, 0, NUM_MEM);
  }
  for (i=0; i<NUM_MEM; ++i) {
    if (1 == USE_LIBC && 1 == USE_LOAD && 1 == USE_LAZY) {
      if (0 == ((uintptr_t)(addr+i)&(page-1)))
        _read(addr+i, i, page);
    }
    assert((char)i == addr[i]);
  }
  _gettime(&te);
  t_rd = _getelapsed(&ts, &te);

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Read (new) ===============\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s        = %9.0f\n", NUM_MEM/(t_rd/1000000.0));
  fprintf(stderr, "  SysPages/s   = %9.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_rd/1000000.0));
  if (0 == USE_LIBC)
    fprintf(stderr, "  # SIGSEGV    = %9zu\n", faults);
  fprintf(stderr, "\n");

  if (1 == USE_LOAD) {
    if (0 == USE_LIBC) {
      ret = mprotect(addr, NUM_MEM, PROT_WRITE);
      assert(0 == ret);
    }
    memset(addr, 0, NUM_MEM);
  }
  if (0 == USE_LIBC) {
    if (0 == USE_LOAD)
      memset(pflags, 0, NUM_MEM/page);
    else
      memset(pflags, ONDISK, NUM_MEM/page);
    ret = mprotect(addr, NUM_MEM, PROT_NONE);
    assert(0 == ret);
    faults = 0;
  }
  _cacheflush();

  /* ----- READ/WRITE ---- */
  _gettime(&ts);
  if (1 == USE_LIBC && 1 == USE_LOAD && 0 == USE_LAZY) {
    _read(addr, 0, NUM_MEM);
  }
  for (i=0; i<NUM_MEM; ++i) {
    if (1 == USE_LIBC && 1 == USE_LOAD && 1 == USE_LAZY) {
      if (0 == ((uintptr_t)(addr+i)&(page-1)))
        _read(addr+i, i, page);
    }
    addr[i]++;
  }
  _gettime(&te);
  t_rw = _getelapsed(&ts, &te);

  for (i=0; i<NUM_MEM; ++i)
    assert((char)(i+1) == addr[i]);

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Read/Write (new) =========\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s        = %9.0f\n", NUM_MEM/(t_rw/1000000.0));
  fprintf(stderr, "  SysPages/s   = %9.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_rw/1000000.0));
  if (0 == USE_LIBC)
    fprintf(stderr, "  # SIGSEGV    = %9zu\n", faults);
  fprintf(stderr, "\n");

  if (1 == USE_LOAD) {
    memset(addr, 0, NUM_MEM);
  }
  if (0 == USE_LIBC) {
    ret = mprotect(addr, NUM_MEM, PROT_NONE);
    assert(0 == ret);
    memset(pflags, 0, NUM_MEM/page);
    faults = 0;
  }
  _cacheflush();

  /* ===== Initialized memory tests ===== */
  /* ----- WRITE ----- */
  _gettime(&ts);
  for (i=0; i<NUM_MEM; ++i)
    addr[i] = (char)(NUM_MEM-i);
  _gettime(&te);
  t_wr = _getelapsed(&ts, &te);

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Write (init) =============\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s        = %9.0f\n", NUM_MEM/(t_wr/1000000.0));
  fprintf(stderr, "  SysPages/s   = %9.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_wr/1000000.0));
  if (0 == USE_LIBC)
    fprintf(stderr, "  # SIGSEGV    = %9zu\n", faults);
  fprintf(stderr, "\n");

  if (1 == USE_LOAD) {
    _write(addr, NUM_MEM);

    memset(addr, 0, NUM_MEM);
  }
  if (0 == USE_LIBC) {
    if (0 == USE_LOAD)
      memset(pflags, 0, NUM_MEM/page);
    else
      memset(pflags, ONDISK, NUM_MEM/page);
    ret = mprotect(addr, NUM_MEM, PROT_NONE);
    assert(0 == ret);
    faults = 0;
  }
  _cacheflush();

  /* ----- READ ----- */
  _gettime(&ts);
  if (1 == USE_LIBC && 1 == USE_LOAD && 0 == USE_LAZY) {
    _read(addr, 0, NUM_MEM);
  }
  for (i=0; i<NUM_MEM; ++i) {
    if (1 == USE_LIBC && 1 == USE_LOAD && 1 == USE_LAZY) {
      if (0 == ((uintptr_t)(addr+i)&(page-1)))
        _read(addr+i, i, page);
    }
    assert((char)(NUM_MEM-i) == addr[i]);
  }
  _gettime(&te);
  t_rd = _getelapsed(&ts, &te);

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Read (init) ==============\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s        = %9.0f\n", NUM_MEM/(t_rd/1000000.0));
  fprintf(stderr, "  SysPages/s   = %9.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_rd/1000000.0));
  if (0 == USE_LIBC)
    fprintf(stderr, "  # SIGSEGV    = %9zu\n", faults);
  fprintf(stderr, "\n");

  if (1 == USE_LOAD) {
    if (0 == USE_LIBC) {
      ret = mprotect(addr, NUM_MEM, PROT_WRITE);
      assert(0 == ret);
    }
    memset(addr, 0, NUM_MEM);
  }
  if (0 == USE_LIBC) {
    if (0 == USE_LOAD)
      memset(pflags, 0, NUM_MEM/page);
    else
      memset(pflags, ONDISK, NUM_MEM/page);
    ret = mprotect(addr, NUM_MEM, PROT_NONE);
    assert(0 == ret);
    faults = 0;
  }
  _cacheflush();

  /* ----- READ/WRITE ----- */
  _gettime(&ts);
  if (1 == USE_LIBC && 1 == USE_LOAD && 0 == USE_LAZY) {
    _read(addr, 0, NUM_MEM);
  }
  for (i=0; i<NUM_MEM; ++i) {
    if (1 == USE_LIBC && 1 == USE_LOAD && 1 == USE_LAZY) {
      if (0 == ((uintptr_t)(addr+i)&(page-1)))
        _read(addr+i, i, page);
    }
    addr[i]++;
  }
  _gettime(&te);
  t_rw = _getelapsed(&ts, &te);

  for (i=0; i<NUM_MEM; ++i)
    assert((char)(NUM_MEM-i+1) == addr[i]);

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Read/Write (init) ========\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s        = %9.0f\n", NUM_MEM/(t_rw/1000000.0));
  fprintf(stderr, "  SysPages/s   = %9.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_rw/1000000.0));
  if (0 == USE_LIBC)
    fprintf(stderr, "  # SIGSEGV    = %9zu\n", faults);

  /* ===== Release resources ===== */
  ret = munmap(addr, NUM_MEM);
  assert(0 == ret);
  if (0 == USE_LIBC) {
    ret = munmap(pflags, NUM_MEM/page);
    assert(0 == ret);
  }
  if (1 == USE_LOAD) {
    ret = close(filed);
    assert(-1 != ret);
    ret = unlink(TMPFILE);
    assert(-1 != ret);
  }

  return EXIT_SUCCESS;
}
