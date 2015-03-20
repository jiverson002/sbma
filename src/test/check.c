#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

/*#define SEED   1426830585*/
/*#define SEED   1426361468*/
#define SEED   time(NULL)
#define USE_KL 1

#if defined(USE_KL) && USE_KL > 0
# include "klmalloc.h"
# define MALLOC       klmalloc
# define FREE         klfree
# define MALLOC_STATS klmalloc_stats
#else
# include <malloc.h>
# define MALLOC       malloc
# define FREE         free
# define MALLOC_STATS malloc_stats
#endif


/* probability for each type of allocation (must sum to 100) */
#define PER_BIG_ALLOC 5
#define PER_MED_ALLOC 35
#define PER_SML_ALLOC 60

/* probability to free a previous allocation */
#define PER_FREE      30

/*size_t NUM_ALLOCS     = 1<<17;*/
size_t NUM_ALLOCS     = 1<<12;
size_t BIG_ALLOC_SIZE = 1<<25; /* 16MB */
size_t MED_ALLOC_SIZE = 1<<16; /* 32KB */
size_t SML_ALLOC_SIZE = 1<<11; /* 1KB  */

int main(void)
{
#if 1
  size_t i, j, k, l, sz;
#endif
  unsigned long ta, tf, seed;
  void * buf;
  void ** alloc;
#if 1
  struct timeval ts, te;
#endif

  seed = SEED;
  srand(seed);

  fprintf(stderr, "seed = %lu\n", seed);

  ta     = 0;
  tf     = 0;
  alloc = (void **) MALLOC(NUM_ALLOCS*sizeof(void *));
  assert(NULL != alloc);
  buf = MALLOC(BIG_ALLOC_SIZE);
  assert(NULL != buf);

#if 1
  for (i=0; i<NUM_ALLOCS; ++i) {
    j = rand()%100; /* indicator for big/med/sml alloc */
    k = rand()%100; /* indicator for free              */

    if (j < PER_BIG_ALLOC)                    /*  5% chance to make big alloc */
      sz = rand()%BIG_ALLOC_SIZE;
    else if (j < PER_BIG_ALLOC+PER_MED_ALLOC) /* 35% chance to make med alloc */
      sz = rand()%MED_ALLOC_SIZE;
    else                                      /* 60% chance to make sml alloc */
      sz = rand()%SML_ALLOC_SIZE;
    sz++;

    gettimeofday(&ts, NULL);
    alloc[i] = MALLOC(sz);
    gettimeofday(&te, NULL);
    ta += (te.tv_sec-ts.tv_sec)*1000000 + te.tv_usec-ts.tv_usec;
    assert(NULL != alloc[i]);

    /* make sure we can read from allocated memory */
    memcpy(buf, alloc[i], sz);
    assert(0 == memcmp(buf, alloc[i], sz));

    if (k < PER_FREE) { /* 30% chance to free memory */
      if (0 == i)
        l = 0;
      else
        l = rand()%i;
      while (l < i && NULL == alloc[l])
        l++;

      if (NULL != alloc[l]) {
        gettimeofday(&ts, NULL);
        FREE(alloc[l]);
        gettimeofday(&te, NULL);
        tf += (te.tv_sec-ts.tv_sec)*1000000 + te.tv_usec-ts.tv_usec;

        alloc[l] = NULL;
      }
    }
  }

  /* free remaining allocs */
  for (i=0; i<NUM_ALLOCS; ++i) {
    if (NULL != alloc[i]) {
      gettimeofday(&ts, NULL);
      FREE(alloc[i]);
      gettimeofday(&te, NULL);
      tf += (te.tv_sec-ts.tv_sec)*1000000 + te.tv_usec-ts.tv_usec;
      alloc[i] = NULL;
    }
  }

  fprintf(stderr, "Time per malloc = %.2f us\n", ta*1.0/NUM_ALLOCS);
  fprintf(stderr, "Time per free   = %.2f us\n", tf*1.0/NUM_ALLOCS);
  MALLOC_STATS();
#endif

  FREE(alloc);
  FREE(buf);

  return EXIT_SUCCESS;
}
