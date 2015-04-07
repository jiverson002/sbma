#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

/*#define SEED   1426830585*/
/*#define SEED   1426361468*/
/*#define SEED   1428422405*/
/*#define SEED   1428425781*/
/*#define SEED   1428431895*/
#define SEED   time(NULL)
#define USE_KL 1

#if defined(USE_KL) && USE_KL > 0
# include "klmalloc.h"
# define MALLOC       KL_malloc
# define FREE         KL_free
# define MALLOC_STATS KL_malloc_stats
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
size_t BIG_ALLOC_SIZE = 1<<25;            /* 16MB */
size_t MED_ALLOC_SIZE = 262144;           /* CHUNK_MAX_SIZE? */
size_t SML_ALLOC_SIZE = 4*sizeof(void*);  /* BRICK_MAX_SIZE  */

int main(void)
{
  size_t i, j, k, l, sz, cur_req=0, max_req=0;
  unsigned long ta, tf, seed;
  void * buf;
  void ** alloc;
  size_t * size;
  struct timeval ts, te;

  seed = SEED;
  srand(seed);

  fprintf(stderr, "seed = %lu\n", seed);
  //printf("<beg>            ----------------------------------------\n");

  ta     = 0;
  tf     = 0;
  alloc = (void **) MALLOC(NUM_ALLOCS*sizeof(void *));
  assert(NULL != alloc);
  size = (size_t *) MALLOC(NUM_ALLOCS*sizeof(size_t));
  assert(NULL != size);
  buf = MALLOC(BIG_ALLOC_SIZE);
  assert(NULL != buf);
  memset(buf, 1, BIG_ALLOC_SIZE);

  //printf("\n");

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

    //printf("<%3zu> %10zu ----------------------------------------\n", i, sz);

    gettimeofday(&ts, NULL);
    alloc[i] = MALLOC(sz);
    gettimeofday(&te, NULL);
    ta += (te.tv_sec-ts.tv_sec)*1000000 + te.tv_usec-ts.tv_usec;
    assert(NULL != alloc[i]);
    memset(alloc[i], 1, sz);

    size[i] = sz;
    cur_req += sz;
    if (cur_req > max_req)
      max_req = cur_req;

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

        cur_req -= size[l];
      }
    }

    //printf("\n");
  }

  //printf("<end>            ----------------------------------------\n\n");

  /* free remaining allocs */
  for (i=0; i<NUM_ALLOCS; ++i) {
    if (NULL != alloc[i]) {
      gettimeofday(&ts, NULL);
      FREE(alloc[i]);
      gettimeofday(&te, NULL);
      tf += (te.tv_sec-ts.tv_sec)*1000000 + te.tv_usec-ts.tv_usec;
      alloc[i] = NULL;
      cur_req -= size[i];
    }
  }

  fprintf(stderr, "Time per malloc = %.2f us\n", ta*1.0/NUM_ALLOCS);
  fprintf(stderr, "Time per free   = %.2f us\n", tf*1.0/NUM_ALLOCS);
  MALLOC_STATS();
  fprintf(stderr, "Maximum concurrent request = %zu\n", max_req);

  FREE(alloc);
  FREE(size);
  FREE(buf);

  return EXIT_SUCCESS;
}
