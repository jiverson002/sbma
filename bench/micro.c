/* ============================ BEG CONFIG ================================ */
/*
#define USE_LIBC

#define USE_SBMA

#define USE_LOAD

#define USE_LAZY

#define USE_GHOST

#define USE_CTX

#define USE_RAND

#define USE_LOCK

#define USE_RD

#define USE_WR

#define USE_RW
*/

#define DEFAULT_NUM_MEM ((1lu<<31)-(1lu<<29)) /* 1.5GiB */

#define DEFAULT_NUM_SYS 1                     /* 4KiB */

#define DEFAULT_TMPFILE "/scratch/micro1"

#define DATATYPE unsigned


/* ======================= INTERNAL CONFIG ================================ */


#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#ifdef NDEBUG
# undef NDEBUG
#endif

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "impl/impl.h"

#define XSTR(X) #X
#define STR(X)  XSTR(X)

static size_t SIZ_PAG = 0;
static size_t NUM_MEM = DEFAULT_NUM_MEM;
static size_t NUM_SYS = DEFAULT_NUM_SYS;
static size_t NUM_PAG = 0;
static char * TMPFILE = DEFAULT_TMPFILE;

extern size_t faults;

#pragma GCC push_options
#pragma GCC optimize("-O0")
static void
_membarrier(void)
{
  __asm volatile("": : :"memory"); \
  __sync_synchronize();
}

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

  SIZ_PAG = sysconf(_SC_PAGESIZE)*NUM_SYS;
  assert(0 == (NUM_MEM&(SIZ_PAG-1)));

  NUM_PAG = NUM_MEM/SIZ_PAG;
}

int main(int argc, char * argv[])
{
  size_t i, j, ii, jj, elem;
  ssize_t ret;
#if defined(USE_RD)
  unsigned long t_rd;
#endif
#if defined(USE_WR)
  unsigned long t_wr;
#endif
#if defined(USE_RW)
  size_t kk;
  unsigned long t_rw;
#endif
  struct timespec ts, te;
  DATATYPE tmp;
  DATATYPE * addr;
  DATATYPE * rnum;

  _parse(argc, argv);

  fprintf(stderr, "==========================\n");
  fprintf(stderr, "General ==================\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  Version      = %9s\n", STR(VERSION));
  fprintf(stderr, "  Build date   = %9s\n", STR(DATE));
  fprintf(stderr, "  Git commit   = %9s\n", STR(COMMIT));
  fprintf(stderr, "  Library      =      %s\n", impl_name());
  fprintf(stderr, "  Datatype     = %9s\n", STR(DATATYPE));
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
#if defined(USE_LOAD)
  fprintf(stderr, "  Temp file    = %s\n", TMPFILE);
#endif
  fprintf(stderr, "\n");

  elem = NUM_MEM/sizeof(DATATYPE);

  /* ===== Acquire resources ===== */
{
  addr = impl_init(NUM_MEM, NUM_SYS);
  assert(MAP_FAILED != addr);

  rnum = mmap(NULL, NUM_MEM, PROT_READ|PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
  assert(MAP_FAILED != rnum);
}

  /* ===== Generate random array ===== */
{
  for (i=0; i<elem; ++i)
    rnum[i] = i;
  for (i=0; i<elem-1; ++i) {
    j = rand()%((elem-1)-i)+i;
    tmp = rnum[j];
    rnum[j] = rnum[i];
    rnum[i] = tmp;
  }
}

  impl_flush();
  _cacheflush();

  /* ===== Popoulate array ===== */
{
  _gettime(&ts);
#if defined(USE_RAND)
    ii = rnum[0];
#else
    ii = 0;
#endif
  addr[ii] = rnum[ii];
  for (i=1; i<elem; ++i) {
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
  for (i=1; i<elem; ++i) {
#if defined(USE_RAND)
    ii = rnum[i];
    jj = rnum[i-1];
#else
    ii = i;
    jj = i-1;
#endif
    assert(addr[jj]+rnum[ii] == addr[ii]);
  }
}

  impl_flush();
  _cacheflush();

  /* ----- WRITE (new) ----- */
#if defined(USE_WR)
{
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "Write (new) ==============\n");
  fprintf(stderr, "==========================\n");
  fprintf(stderr, "  MiB/s        = %9.0f\n", NUM_MEM/(t_wr/1000000.0));
  fprintf(stderr, "  SysPages/s   = %9.0f\n",
    NUM_MEM/sysconf(_SC_PAGESIZE)/(t_wr/1000000.0));
  impl_aux_info(12, 9);
  fprintf(stderr, "\n");

  impl_flush();
  _cacheflush();
}
#endif

  /* ----- WRITE (init) ----- */
#if defined(USE_WR)
{
  _gettime(&ts);
#if defined(USE_RAND)
    ii = rnum[0];
#else
    ii = 0;
#endif
  addr[ii] = rnum[ii];
  for (i=1; i<elem; ++i) {
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
  for (i=1; i<elem; ++i) {
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
  impl_aux_info(12, 9);
  fprintf(stderr, "\n");
}
#endif

  /* ===== File setup ===== */
#if defined(USE_LOAD)
{
  io_init(TMPFILE, NUM_MEM);

  io_write(addr, NUM_MEM);

  memset(addr, 0, NUM_MEM);

  impl_ondisk();
}
#endif

  /* ===== Flush resources ===== */
#if defined(USE_WR) || defined(USE_LOAD)
{
  impl_flush();
  io_flush();
  _cacheflush();
}
#endif

  /* ----- READ ----- */
#if defined(USE_RD)
{
  _gettime(&ts);
  impl_fetch_bulk(addr, 0, NUM_MEM);
#if defined(USE_RAND)
  ii = rnum[0];
#else
  ii = 0;
#endif
  impl_fetch_page(addr+ii, ii*sizeof(DATATYPE), SIZ_PAG);
  assert(rnum[ii] == addr[ii]);
  for (i=1; i<elem; ++i) {
#if defined(USE_RAND)
    ii = rnum[i];
    jj = rnum[i-1];
#else
    ii = i;
    jj = i-1;
#endif
    impl_fetch_page(addr+ii, ii*sizeof(DATATYPE), SIZ_PAG);
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
  impl_aux_info(12, 9);
  fprintf(stderr, "\n");

  impl_flush();
  io_flush();
  _cacheflush();
}
#endif

  /* ----- READ/WRITE ----- */
#if defined(USE_RW)
{
  _gettime(&ts);
  impl_fetch_bulk(addr, 0, NUM_MEM);
#if defined(USE_RAND)
  ii = rnum[0];
#else
  ii = 0;
#endif
  impl_fetch_page(addr+ii, ii*sizeof(DATATYPE), SIZ_PAG);
  addr[ii] += rnum[ii];
  for (i=1; i<elem; ++i) {
#if defined(USE_RAND)
    ii = rnum[i];
    jj = rnum[i-1];
#else
    ii = i;
    jj = i-1;
#endif
    impl_fetch_page(addr+ii, ii*sizeof(DATATYPE), SIZ_PAG);
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
  for (i=2; i<elem; ++i) {
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
  impl_aux_info(12, 9);
}
#endif

  /* ===== Release resources ===== */
{
  impl_destroy();
  io_destroy();

  ret = munmap(rnum, NUM_MEM);
  assert(0 == ret);
}

  return EXIT_SUCCESS;
}
