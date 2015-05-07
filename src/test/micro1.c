#ifndef _BSD_SOURCE
# define _BSD_SOURCE
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
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>


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


#ifndef USE_LIBC
static char * pflags=NULL;
static uintptr_t base=0;
static size_t page=0;
#define SYNC  1
#define DIRTY 2

static void
_segvhandler(int const sig, siginfo_t * const si, void * const ctx)
{
  int ret;
  uintptr_t addr;
  size_t ip;

  assert(SIGSEGV == sig);

  ip   = ((uintptr_t)si->si_addr-base)/page;
  addr = base+ip*page;

  if (SYNC != (pflags[ip]&SYNC)) {
    ret = mprotect((void*)addr, page, PROT_READ);
    assert(0 == ret);
    pflags[ip] = SYNC;
  }
  else if (SYNC == (pflags[ip]&SYNC)) {
    ret = mprotect((void*)addr, page, PROT_READ|PROT_WRITE);
    assert(0 == ret);
    pflags[ip] = DIRTY;
  }
  else {
    assert(0);
  }

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
#endif


int main(void)
{
  int ret;
  unsigned long t_rd, t_wr;
  size_t i;
  struct _timespec ts, te;
  char * buf;

  size_t const NUM_MEM = (1lu<<32)-(1lu<<30); /* 3.0GiB */
  //size_t const NUM_MEM = (1lu<<30);           /* 1.0GiB */
#ifndef USE_LIBC
  size_t const NUM_SYS = 4;                   /* 4KiB */
#endif

  assert(0 == (NUM_MEM&(sysconf(_SC_PAGESIZE)-1)));

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "General ==================\n");
  fprintf(stderr, "==========================\n");
#ifndef USE_LIBC
  fprintf(stderr, "Library      =        sbma\n");
#else
  fprintf(stderr, "Library      =        libc\n");
#endif
  fprintf(stderr, "MiB I/O      = %11.0f\n", NUM_MEM/1000000.0);
  fprintf(stderr, "SysPages I/O = %11lu\n",
    NUM_MEM/sysconf(_SC_PAGESIZE));
  fprintf(stderr, "\n");

#ifndef USE_LIBC
  _init();

  buf = mmap(NULL, NUM_MEM, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != buf);

  base = (uintptr_t)buf;
  page = sysconf(_SC_PAGESIZE)*NUM_SYS;
  assert(0 == (NUM_MEM&(page-1)));

  pflags = mmap(NULL, NUM_MEM/page, PROT_READ|PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != pflags);
#else
  buf = mmap(NULL, NUM_MEM, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != buf);
#endif

  _cacheflush();

  /* ===== Uninitialized memory tests ===== */
  _gettime(&ts);
  for (i=0; i<NUM_MEM; ++i)
    buf[i] = (char)i;
  _gettime(&te);
  t_wr = _getelapsed(&ts, &te);

  _cacheflush();

#ifndef USE_LIBC
  ret = mprotect(buf, NUM_MEM, PROT_NONE);
  assert(0 == ret);
  memset(pflags, 0, NUM_MEM/page);
#endif

  _cacheflush();

  _gettime(&ts);
  for (i=0; i<NUM_MEM; ++i)
    assert((char)i == buf[i]);
  _gettime(&te);
  t_rd = _getelapsed(&ts, &te);

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Read (new) ===============\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s      = %11.0f\n",
    NUM_MEM/(t_rd/1000000.0));
  fprintf(stderr, "  SysPages/s = %11.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_rd/1000000.0));
  fprintf(stderr, "\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Write (new) ==============\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s      = %11.0f\n",
    NUM_MEM/(t_wr/1000000.0));
  fprintf(stderr, "  SysPages/s = %11.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_wr/1000000.0));
  fprintf(stderr, "\n");

  _cacheflush();

  /* ===== Initialized memory tests ===== */
  _gettime(&ts);
  for (i=0; i<NUM_MEM; ++i)
    buf[i] = (char)(NUM_MEM-i);
  _gettime(&te);
  t_wr = _getelapsed(&ts, &te);

  _cacheflush();

#ifndef USE_LIBC
  ret = mprotect(buf, NUM_MEM, PROT_NONE);
  assert(0 == ret);
  memset(pflags, 0, NUM_MEM/page);
#endif

  _cacheflush();

  _gettime(&ts);
  for (i=0; i<NUM_MEM; ++i)
    assert((char)(NUM_MEM-i) == buf[i]);
  _gettime(&te);
  t_rd = _getelapsed(&ts, &te);

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Read (init) ==============\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s      = %11.0f\n",
    NUM_MEM/(t_rd/1000000.0));
  fprintf(stderr, "  SysPages/s = %11.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_rd/1000000.0));
  fprintf(stderr, "\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Write (init) =============\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s      = %11.0f\n",
    NUM_MEM/(t_wr/1000000.0));
  fprintf(stderr, "  SysPages/s = %11.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_wr/1000000.0));

  /* ===== Release memory ===== */
  ret = munmap(buf, NUM_MEM);
  assert(0 == ret);
#ifndef USE_LIBC
  ret = munmap(pflags, NUM_MEM/page);
  assert(0 == ret);
#endif

  return EXIT_SUCCESS;
}
