#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#ifdef NDEBUG
# undef NDEBUG
#endif

#define _POSIX_C_SOURCE 200112L
#if !defined(_WIN32) && (defined(__unix__) || defined(__unix) || \
    (defined(__APPLE__) && defined(__MACH__)))
# include <unistd.h>
#endif

/* try to get access to clock_gettime() */
#if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0 && defined(_POSIX_MONOTONIC_CLOCK)
# include <time.h>
# define HAVE_CLOCK_GETTIME
/* then try to get access to gettimeofday() */
#elif defined(_POSIX_VERSION)
# undef _POSIX_C_SOURCE
# include <sys/time.h>
# define HAVE_GETTIMEOFDAY
/* if neither of the above are available, default to time() */
#else
# undef _POSIX_C_SOURCE
# include <time.h>
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


struct _timespec
{
  long unsigned tv_sec;
  long unsigned tv_nsec;
};

static void
_gettime(struct _timespec * const t)
{
#if defined(HAVE_CLOCK_GETTIME)
  struct timespec tt;
  clock_gettime(CLOCK_MONOTONIC, &tt);
  t->tv_sec = tt.tv_sec;
  t->tv_nsec = tt.tv_nsec;
#elif defined(HAVE_GETTIMEOFDAY)
  struct timeval tt;
  gettimeofday(&tt, NULL);
  t->tv_sec = tt.tv_sec;
  t->tv_nsec = tt.tv_usec * 1000UL;
#else
  time_t tt = time(NULL);
  t->tv_sec = tt;
  t->tv_nsec = 0;
#endif
}

static long unsigned
_getelapsed(struct _timespec const * const ts,
            struct _timespec const * const te)
{
  struct _timespec t;
  if (te->tv_nsec < ts->tv_nsec) {
    t.tv_nsec = 1000000000UL + te->tv_nsec - ts->tv_nsec;
    t.tv_sec = te->tv_sec - 1 - ts->tv_sec;
  }else {
    t.tv_nsec = te->tv_nsec - ts->tv_nsec;
    t.tv_sec = te->tv_sec - ts->tv_sec;
  }
  return (unsigned long)(t.tv_sec * 1000000000UL + t.tv_nsec);
}


#pragma GCC push_options
#pragma GCC optimize("-O0")
static void
_cacheflush(void)
{
  /* 1<<28 == 256MiB */
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
}
#pragma GCC pop_options


/* ============================ BEG CONFIG ================================ */
static int const USE_LOAD         = 1;
static int const USE_LAZY         = 1;
static int const USE_LIBC         = 1;

//static size_t const NUM_MEM       = (1lu<<32)-(1lu<<30); /* 3.0GiB */
static size_t const NUM_MEM       = (1lu<<28);           /* 1.0GiB */
static size_t const NUM_SYS       = 1;                   /* 4KiB */
static char const * const TMPFILE = "/scratch/micro2";
/* ============================ END CONFIG ================================ */

static char * pflags=NULL;
static uintptr_t base=0;
static size_t page=0;
static size_t faults=0;
#define SYNC   1
#define DIRTY  2
#define ONDISK 4

static void
_segvhandler(int const sig, siginfo_t * const si, void * const ctx)
{
  int fd;
  size_t size, len, off;
  ssize_t ret;
  uintptr_t addr, new;
  void * tmp_addr;
  char * buf;
  size_t ip;

  assert(SIGSEGV == sig);

  ip   = ((uintptr_t)si->si_addr-base)/page;
  addr = base+ip*page;

  if (SYNC != (pflags[ip]&SYNC)) {
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

      fd = open(TMPFILE, O_RDONLY);
      assert(-1 != ret);

      tmp_addr = mmap(NULL, len, PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
      assert(MAP_FAILED != tmp_addr);

      ret = lseek(fd, off, SEEK_SET);
      assert(-1 != ret);

      buf  = tmp_addr;
      size = len;
      do {
        ret = read(fd, buf, size);
        assert(-1 != ret);

        buf  += ret;
        size -= ret;
      } while (size > 0);

      //printf("mprotect(%zx, %zu, RD) [%x]\n", new, len, pflags[ip]);
      ret = mprotect(tmp_addr, len, PROT_READ);
      assert(-1 != ret);

      tmp_addr = mremap(tmp_addr, len, len, MREMAP_MAYMOVE|MREMAP_FIXED, new);
      assert(MAP_FAILED != tmp_addr);

      ret = close(fd);
      assert(-1 != ret);
      /* ========================= END LOAD =============================== */
    }
    else if (1 == USE_LAZY) {
      //printf("mprotect(%zx, %zu, RD [%x])\n", addr, page, pflags[ip]);
      ret = mprotect((void*)addr, page, PROT_READ);
      assert(0 == ret);
    }
    else {
      //printf("mprotect(%zx, %zu, RD [%x])\n", addr, NUM_MEM, pflags[ip]);
      ret = mprotect((void*)addr, NUM_MEM, PROT_READ);
      assert(0 == ret);
    }

    if (1 == USE_LOAD && 1 == USE_LAZY)
      pflags[ip] = SYNC;
    else if (1 == USE_LOAD)
      memset(pflags, SYNC, NUM_MEM/page);
  }
  else if (SYNC == (pflags[ip]&SYNC)) {
    //printf("mprotect(%zx, %zu, RD|WR) [%x]\n", addr, page, pflags[ip]);
    ret = mprotect((void*)addr, page, PROT_READ|PROT_WRITE);
    assert(0 == ret);

    pflags[ip] = DIRTY;
  }
  else {
    assert(0);
  }

  faults++;

  if (NULL == ctx) {} /* suppress unused warning */
}

static void
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

int main(void)
{
  int fd;
  size_t size;
  ssize_t ret;
  unsigned long t_rd, t_wr, t_rw;
  size_t i;
  struct _timespec ts, te;
  char * addr, * buf;

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "General ==================\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  Library      =      %s\n", 1==USE_LIBC?"libc":"sbma");
  fprintf(stderr, "  MiB I/O      = %9.0f\n", NUM_MEM/1000000.0);
  fprintf(stderr, "  SysPages I/O = %9lu\n", NUM_MEM/sysconf(_SC_PAGESIZE));
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
    fd = open(TMPFILE, O_WRONLY|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);
    assert(-1 != ret);

    buf  = addr;
    size = NUM_MEM;
    do {
      ret = write(fd, buf, size);
      assert(-1 != ret);

      buf  += ret;
      size -= ret;
    } while (size > 0);

    ret = close(fd);
    assert(-1 != ret);

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
    fd = open(TMPFILE, O_RDONLY);
    assert(-1 != ret);

    buf  = addr;
    size = NUM_MEM;
    do {
      ret = read(fd, buf, size);
      assert(-1 != ret);

      buf  += ret;
      size -= ret;
    } while (size > 0);

    ret = close(fd);
    assert(-1 != ret);
  }
  for (i=0; i<NUM_MEM; ++i) {
    if (1 == USE_LIBC && 1 == USE_LOAD && 1 == USE_LAZY) {
      if (0 == ((uintptr_t)(addr+i)&(page-1))) {
        fd = open(TMPFILE, O_RDONLY);
        assert(-1 != ret);

        ret = lseek(fd, i, SEEK_SET);
        assert(-1 != ret);

        buf  = addr+i;
        size = page;
        do {
          ret = read(fd, buf, size);
          assert(-1 != ret);

          buf  += ret;
          size -= ret;
        } while (size > 0);

        ret = close(fd);
        assert(-1 != ret);
      }
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
    fd = open(TMPFILE, O_RDONLY);
    assert(-1 != ret);

    buf  = addr;
    size = NUM_MEM;
    do {
      ret = read(fd, buf, size);
      assert(-1 != ret);

      buf  += ret;
      size -= ret;
    } while (size > 0);

    ret = close(fd);
    assert(-1 != ret);
  }
  for (i=0; i<NUM_MEM; ++i) {
    if (1 == USE_LIBC && 1 == USE_LOAD && 1 == USE_LAZY) {
      if (0 == ((uintptr_t)(addr+i)&(page-1))) {
        fd = open(TMPFILE, O_RDONLY);
        assert(-1 != ret);

        ret = lseek(fd, i, SEEK_SET);
        assert(-1 != ret);

        buf  = addr+i;
        size = page;
        do {
          ret = read(fd, buf, size);
          assert(-1 != ret);

          buf  += ret;
          size -= ret;
        } while (size > 0);

        ret = close(fd);
        assert(-1 != ret);
      }
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
    fd = open(TMPFILE, O_WRONLY);
    assert(-1 != ret);

    buf  = addr;
    size = NUM_MEM;
    do {
      ret = write(fd, buf, size);
      assert(-1 != ret);

      buf  += ret;
      size -= ret;
    } while (size > 0);

    ret = close(fd);
    assert(-1 != ret);

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
    fd = open(TMPFILE, O_RDONLY);
    assert(-1 != ret);

    buf  = addr;
    size = NUM_MEM;
    do {
      ret = read(fd, buf, size);
      assert(-1 != ret);

      buf  += ret;
      size -= ret;
    } while (size > 0);

    ret = close(fd);
    assert(-1 != ret);
  }
  for (i=0; i<NUM_MEM; ++i) {
    if (1 == USE_LIBC && 1 == USE_LOAD && 1 == USE_LAZY) {
      if (0 == ((uintptr_t)(addr+i)&(page-1))) {
        fd = open(TMPFILE, O_RDONLY);
        assert(-1 != ret);

        ret = lseek(fd, i, SEEK_SET);
        assert(-1 != ret);

        buf  = addr+i;
        size = page;
        do {
          ret = read(fd, buf, size);
          assert(-1 != ret);

          buf  += ret;
          size -= ret;
        } while (size > 0);

        ret = close(fd);
        assert(-1 != ret);
      }
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
    fd = open(TMPFILE, O_RDONLY);
    assert(-1 != ret);

    buf  = addr;
    size = NUM_MEM;
    do {
      ret = read(fd, buf, size);
      assert(-1 != ret);

      buf  += ret;
      size -= ret;
    } while (size > 0);

    ret = close(fd);
    assert(-1 != ret);
  }
  for (i=0; i<NUM_MEM; ++i) {
    if (1 == USE_LIBC && 1 == USE_LOAD && 1 == USE_LAZY) {
      if (0 == ((uintptr_t)(addr+i)&(page-1))) {
        fd = open(TMPFILE, O_RDONLY);
        assert(-1 != ret);

        ret = lseek(fd, i, SEEK_SET);
        assert(-1 != ret);

        buf  = addr+i;
        size = page;
        do {
          ret = read(fd, buf, size);
          assert(-1 != ret);

          buf  += ret;
          size -= ret;
        } while (size > 0);

        ret = close(fd);
        assert(-1 != ret);
      }
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
    ret = unlink(TMPFILE);
    assert(-1 != ret);
  }

  return EXIT_SUCCESS;
}
