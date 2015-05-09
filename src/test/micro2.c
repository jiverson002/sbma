/*===========================================================================*/
#ifndef _BSD_SOURCE
# define _BSD_SOURCE
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

#include <sys/mman.h>

struct _timespec
{
  long unsigned tv_sec;
  long unsigned tv_nsec;
};

void
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

long unsigned
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
void
_cacheflush(void)
{
  long unsigned i;
  char * ptr = mmap(NULL, 1LU<<32, PROT_READ|PROT_WRITE,
    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (MAP_FAILED != ptr) {
    for (i=0; i<1LU<<32; ++i)
      ptr[i] = i;
    munmap(ptr, 1LU<<32);
  }
}
#pragma GCC pop_options
/*===========================================================================*/


#ifdef NDEBUG
# undef NDEBUG
# include <assert.h>
# define NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "klmalloc.h"


/* random number generator seed */
//#define SEED time(NULL)
//#define SEED 1430937600
#define SEED 1430939513


/* use standard c malloc library or klmalloc library */
#ifdef USE_LIBC
# define PFX(F) libc_##F
# ifdef __cplusplus
extern "C" {
# endif
void * libc_malloc(size_t);
void libc_free(void*);
# ifdef __cplusplus
}
# endif
#else
# include <sbmalloc.h>
# define PFX(F) KL_##F
#endif


/* probability to memset a previous allocation */
#define PER_MEMSET 30
/* probability to free a previous allocation */
#define PER_FREE   30


/* different op classes */
enum {
  NOOP,
  FREE,
  ALLOC,
  NUM_CLASS_OP
};


/* different allocation classes */
enum {
  BRICK_ALLOC,
  CHUNK_ALLOC,
  SOLO_ALLOC,
  NUM_CLASS_ALLOC
};


int main(void)
{
  size_t i, j, k, l, m, cur_mem=0;;
  size_t n_alloc=0, n_free=0, n_memset=0, b_alloc=0, b_free=0, b_memset=0;
  size_t b_brick_alloc=0, b_chunk_alloc=0, b_solo_alloc=0;
  size_t b_brick_free=0, b_chunk_free=0, b_solo_free=0;
  size_t b_brick_memset=0, b_chunk_memset=0, b_solo_memset=0;
  size_t n_brick_alloc=0, n_chunk_alloc=0, n_solo_alloc=0;
  size_t n_brick_free=0, n_chunk_free=0, n_solo_free=0;
  size_t n_brick_memset=0, n_chunk_memset=0, n_solo_memset=0;
  unsigned long t_total=0, seed, _t;
  unsigned long t_alloc=0, t_free=0, t_new_memset=0, t_init_memset=0;
  unsigned long t_brick_alloc=0,  t_chunk_alloc=0,  t_solo_alloc=0;
  unsigned long t_brick_free=0,   t_chunk_free=0,   t_solo_free=0;
  unsigned long t_new_brick_memset=0, t_new_chunk_memset=0, t_new_solo_memset=0;
  unsigned long t_init_brick_memset=0, t_init_chunk_memset=0, t_init_solo_memset=0;
  struct _timespec ts, te, _ts, _te;
  char * op, * buf;
  size_t * oprnd;
  void ** alloc;

  size_t NUM_OPS                = 1<<15;
  size_t const BRICK_ALLOC_SIZE = KL_brick_max_size();
  size_t const CHUNK_ALLOC_SIZE = KL_chunk_max_size();
  size_t const SOLO_ALLOC_SIZE  = 1lu<<24;              /* 16MB */
  size_t const MAX_MEM          = 1lu<<32;              /* 4GiB */

  seed = SEED;
  srand(seed);
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "General =======================\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "Seed              = %11lu\n", seed);

  op = (char *) PFX(malloc)(NUM_OPS*sizeof(char));
  assert(NULL != op);
  oprnd = (size_t *) PFX(malloc)(NUM_OPS*sizeof(size_t));
  assert(NULL != oprnd);
  alloc = (void **) PFX(malloc)(NUM_OPS*sizeof(void *));
  assert(NULL != alloc);
  buf = (char *) PFX(malloc)(SOLO_ALLOC_SIZE*sizeof(char));
  assert(NULL != buf);
  memset(buf, 1, SOLO_ALLOC_SIZE);

  for (i=0; i<NUM_OPS; ++i) {
    op[i] = NOOP;

    if ((j=rand()%100) < PER_FREE) {
      k = rand()%(i+1);
      m = i;
      for (l=k; l<i; ++l) {
        if ((void*)1 == alloc[l]) {
          m = l;
          break;
        }
      }
      if (i == m) {
        for (l=0; l<k; ++l) {
          if ((void*)1 == alloc[l]) {
            m = l;
            break;
          }
        }
      }

      if (i != m) {
        op[i]    = FREE;
        oprnd[i] = m;
        alloc[i] = (void*)0;  /* set my alloc as invaild.    */
        alloc[m] = (void*)0;  /* set oprnd alloc as invalid. */

        cur_mem -= oprnd[m];
      }
    }

    if (NOOP == op[i])  {
      switch (rand()%NUM_CLASS_ALLOC) {
        case BRICK_ALLOC:
          j = rand()%BRICK_ALLOC_SIZE;
          break;

        case CHUNK_ALLOC:
          j = rand()%CHUNK_ALLOC_SIZE;
          break;

        case SOLO_ALLOC:
          j = rand()%SOLO_ALLOC_SIZE;
          break;
      }

      if (cur_mem+j+1 <= MAX_MEM) {
        op[i]    = ALLOC;
        oprnd[i] = ++j;
        alloc[i] = (void*)1;

        cur_mem += oprnd[i];
      }
      else {
        break;
      }
    }
  }
  NUM_OPS = i;

  _gettime(&ts);
  for (i=0; i<NUM_OPS; ++i) {
    switch (op[i]) {
      case FREE:
        _gettime(&_ts);
        PFX(free)(alloc[oprnd[i]]);
        alloc[oprnd[i]] = NULL;
        _gettime(&_te);
        _t = _getelapsed(&_ts, &_te);

        if (oprnd[oprnd[i]] <= BRICK_ALLOC_SIZE) {
          t_brick_free += _t;
          b_brick_free += oprnd[oprnd[i]];
          n_brick_free++;
        }
        else if (oprnd[oprnd[i]] <= CHUNK_ALLOC_SIZE) {
          t_chunk_free += _t;
          b_chunk_free += oprnd[oprnd[i]];
          n_chunk_free++;
        }
        else {
          t_solo_free += _t;
          b_solo_free += oprnd[oprnd[i]];
          n_solo_free++;
        }
        break;

      case ALLOC:
        _gettime(&_ts);
        alloc[i] = PFX(malloc)(oprnd[i]);
        assert(NULL != alloc[i]);
        _gettime(&_te);
        _t = _getelapsed(&_ts, &_te);

        if (oprnd[i] <= BRICK_ALLOC_SIZE) {
          t_brick_alloc += _t;
          b_brick_alloc += oprnd[i];
          n_brick_alloc++;
        }
        else if (oprnd[i] <= CHUNK_ALLOC_SIZE) {
          t_chunk_alloc += _t;
          b_chunk_alloc += oprnd[i];
          n_chunk_alloc++;
        }
        else {
          t_solo_alloc += _t;
          b_solo_alloc += oprnd[i];
          n_solo_alloc++;
        }
        break;
    }
  }
  _gettime(&te);
  t_total += _getelapsed(&ts, &te);

  _cacheflush();

  /* Memset new buffers */
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i]) {
      if (oprnd[i] <= BRICK_ALLOC_SIZE) {
        b_brick_memset += oprnd[i];
        n_brick_memset++;

        b_brick_free += oprnd[i];
        n_brick_free++;
      }
      else if (oprnd[i] <= CHUNK_ALLOC_SIZE) {
        b_chunk_memset += oprnd[i];
        n_chunk_memset++;

        b_chunk_free += oprnd[i];
        n_chunk_free++;
      }
      else {
        b_solo_memset += oprnd[i];
        n_solo_memset++;

        b_solo_free += oprnd[i];
        n_solo_free++;
      }
    }
  }
  _gettime(&ts);
  _gettime(&_ts);
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i] && oprnd[i] <= BRICK_ALLOC_SIZE)
      memset(alloc[i], 1, oprnd[i]);
  }
  _gettime(&_te);
  t_new_brick_memset = _getelapsed(&_ts, &_te);

  _cacheflush();

  _gettime(&_ts);
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i] && oprnd[i] > BRICK_ALLOC_SIZE &&
      oprnd[i] <= CHUNK_ALLOC_SIZE)
      memset(alloc[i], 1, oprnd[i]);
  }
  _gettime(&_te);
  t_new_chunk_memset = _getelapsed(&_ts, &_te);

  _cacheflush();

  _gettime(&_ts);
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i] && oprnd[i] > BRICK_ALLOC_SIZE &&
      oprnd[i] > CHUNK_ALLOC_SIZE)
      memset(alloc[i], 1, oprnd[i]);
  }
  _gettime(&_te);
  t_new_solo_memset = _getelapsed(&_ts, &_te);
  _gettime(&te);
  t_total += _getelapsed(&ts, &te);
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i])
      assert(0 == memcmp(buf, alloc[i], oprnd[i]));
  }

#ifndef USE_LIBC
  SB_dumpall();
#endif

  memset(buf, 2, SOLO_ALLOC_SIZE);
  _cacheflush();

  /* Memset initialized buffers */
  _gettime(&ts);
  _gettime(&_ts);
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i] && oprnd[i] <= BRICK_ALLOC_SIZE)
      memset(alloc[i], 2, oprnd[i]);
  }
  _gettime(&_te);
  t_init_brick_memset = _getelapsed(&_ts, &_te);

  _cacheflush();

  _gettime(&_ts);
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i] && oprnd[i] > BRICK_ALLOC_SIZE &&
      oprnd[i] <= CHUNK_ALLOC_SIZE)
      memset(alloc[i], 2, oprnd[i]);
  }
  _gettime(&_te);
  t_init_chunk_memset = _getelapsed(&_ts, &_te);

  _cacheflush();

  _gettime(&_ts);
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i] && oprnd[i] > BRICK_ALLOC_SIZE &&
      oprnd[i] > CHUNK_ALLOC_SIZE)
      memset(alloc[i], 2, oprnd[i]);
  }
  _gettime(&_te);
  t_init_solo_memset = _getelapsed(&_ts, &_te);
  _gettime(&te);
  t_total += _getelapsed(&ts, &te);
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i])
      assert(0 == memcmp(buf, alloc[i], oprnd[i]));
  }

  /* Free remaining allocs */
  _gettime(&ts);
  _gettime(&_ts);
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i] && oprnd[i] <= BRICK_ALLOC_SIZE) {
      PFX(free)(alloc[i]);
      alloc[i] = NULL;
    }
  }
  _gettime(&_te);
  t_brick_free = _getelapsed(&_ts, &_te);

  _cacheflush();

  _gettime(&_ts);
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i] && oprnd[i] > BRICK_ALLOC_SIZE &&
      oprnd[i] <= CHUNK_ALLOC_SIZE)
    {
      PFX(free)(alloc[i]);
      alloc[i] = NULL;
    }
  }
  _gettime(&_te);
  t_chunk_free = _getelapsed(&_ts, &_te);

  _cacheflush();

  _gettime(&_ts);
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i] && oprnd[i] > BRICK_ALLOC_SIZE &&
      oprnd[i] > CHUNK_ALLOC_SIZE)
    {
      PFX(free)(alloc[i]);
      alloc[i] = NULL;
    }
  }
  _gettime(&_te);
  t_solo_free = _getelapsed(&_ts, &_te);
  _gettime(&te);
  t_total += _getelapsed(&ts, &te);

  PFX(free)(op);
  PFX(free)(oprnd);
  PFX(free)(alloc);
  PFX(free)(buf);

  /* Output */
  t_alloc       = t_brick_alloc+t_chunk_alloc+t_solo_alloc;
  t_free        = t_brick_free+t_chunk_free+t_solo_free;
  t_new_memset  = t_new_brick_memset+t_new_chunk_memset+t_new_solo_memset;
  t_init_memset = t_init_brick_memset+t_init_chunk_memset+t_init_solo_memset;

  b_alloc  = b_brick_alloc+b_chunk_alloc+b_solo_alloc;
  b_free   = b_brick_free+b_chunk_free+b_solo_free;
  b_memset = b_brick_memset+b_chunk_memset+b_solo_memset;
  n_alloc  = n_brick_alloc+n_chunk_alloc+n_solo_alloc;
  n_free   = n_brick_free+n_chunk_free+n_solo_free;
  n_memset = n_brick_memset+n_chunk_memset+n_solo_memset;

  fprintf(stderr, "Num ops           = %11zu\n", NUM_OPS);
  fprintf(stderr, "Total time        = %11lu ns\n", t_total);
  fprintf(stderr, "Overhead time     = %11lu ns\n",
    t_total-t_alloc-t_free-t_new_memset-t_init_memset);
  fprintf(stderr, "\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "Malloc ========================\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "  Malloc time     = %11lu ns\n", t_alloc);
  fprintf(stderr, "  Malloc ops      = %11zu\n", n_alloc);
  fprintf(stderr, "  Malloc ns/KiB   = %11lu\n", t_alloc/(b_alloc/1024));
  fprintf(stderr, "    brick ns/KiB  = %11lu\n",
    t_brick_alloc/(b_brick_alloc/1024));
  fprintf(stderr, "    chunk ns/KiB  = %11lu\n",
    t_chunk_alloc/(b_chunk_alloc/1024));
  fprintf(stderr, "    solo ns/KiB   = %11lu\n",
    t_solo_alloc/(b_solo_alloc/1024));
  fprintf(stderr, "\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "Free ==========================\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "  Free time       = %11lu ns\n", t_free);
  fprintf(stderr, "  Free ops        = %11zu\n", n_free);
  fprintf(stderr, "  Free ns/KiB     = %11lu\n", t_free/(b_free/1024));
  fprintf(stderr, "    brick ns/KiB  = %11lu\n",
    t_brick_free/(b_brick_free/1024));
  fprintf(stderr, "    chunk ns/KiB  = %11lu\n",
    t_chunk_free/(b_chunk_free/1024));
  fprintf(stderr, "    solo ns/KiB   = %11lu\n",
    t_solo_free/(b_solo_free/1024));
  fprintf(stderr, "\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "Memset ========================\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "  -----------------------------\n");
  fprintf(stderr, "  New -------------------------\n");
  fprintf(stderr, "  -----------------------------\n");
  fprintf(stderr, "  Memset time     = %11lu ns\n", t_new_memset);
  fprintf(stderr, "  Memset ops      = %11zu\n", n_memset);
  fprintf(stderr, "  Memset ns/KiB   = %11lu\n", t_new_memset/(b_memset/1024));
  fprintf(stderr, "    brick ns/KiB  = %11lu\n",
    t_new_brick_memset/(b_brick_memset/1024));
  fprintf(stderr, "    chunk ns/KiB  = %11lu\n",
    t_new_chunk_memset/(b_chunk_memset/1024));
  fprintf(stderr, "    solo ns/KiB   = %11lu\n",
    t_new_solo_memset/(b_solo_memset/1024));
  fprintf(stderr, "  -----------------------------\n");
  fprintf(stderr, "  Init ------------------------\n");
  fprintf(stderr, "  -----------------------------\n");
  fprintf(stderr, "  Memset time     = %11lu ns\n", t_init_memset);
  fprintf(stderr, "  Memset ops      = %11zu\n", n_memset);
  fprintf(stderr, "  Memset ns/KiB   = %11lu\n", t_init_memset/(b_memset/1024));
  fprintf(stderr, "    brick ns/KiB  = %11lu\n",
    t_init_brick_memset/(b_brick_memset/1024));
  fprintf(stderr, "    chunk ns/KiB  = %11lu\n",
    t_init_chunk_memset/(b_chunk_memset/1024));
  fprintf(stderr, "    solo ns/KiB   = %11lu\n",
    t_init_solo_memset/(b_solo_memset/1024));

  return EXIT_SUCCESS;
}
