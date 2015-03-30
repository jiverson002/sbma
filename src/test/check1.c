#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "klmalloc.h"

/*#define SEED 1426830585*/
/*#define SEED 1426361468*/
/*#define SEED 1427728812*/
#define SEED time(NULL)

/* probability to free a previous allocation */
#define PER_FREE 30

/*#define PFX(F) F*/
#define PFX(F) KL_##F

size_t NUM_ALLOCS = 1<<17;

int main(void)
{
  size_t i, j, k, l, sz;
  unsigned long t_alloc=0, t_free=0, seed;
  void ** alloc;
  size_t * size;
  struct timeval t_beg1, t_beg2, t_end1, t_end2;

  seed = SEED;
  srand(seed);

  fprintf(stderr, "seed = %lu\n", seed);

  alloc = (void **) malloc(NUM_ALLOCS*sizeof(void *));
  assert(NULL != alloc);
  size = (size_t *) malloc(NUM_ALLOCS*sizeof(size_t));
  assert(NULL != size);

  gettimeofday(&t_beg1, NULL);
  gettimeofday(&t_beg2, NULL);
  for (i=0; i<NUM_ALLOCS; ++i) {
    j = rand()%100; /* indicator for big/med/sml alloc */
    k = rand()%100; /* indicator for free              */

    sz = j%(2*sizeof(void*)-1)+1;

    alloc[i] = PFX(malloc)(sz);
    assert(NULL != alloc[i]);

    if (k < PER_FREE) { /* 30% chance to free memory */
      gettimeofday(&t_end2, NULL);
      t_alloc +=
        (t_end2.tv_sec-t_beg2.tv_sec)*1000000+(t_end2.tv_usec-t_beg2.tv_usec);

      if (0 == i)
        l = 0;
      else
        l = rand()%i;
      while (l < i && NULL == alloc[l])
        l++;

      if (NULL != alloc[l]) {
        PFX(free)(alloc[l]);
        alloc[l] = NULL;
      }

      gettimeofday(&t_beg2, NULL);
    }
  }
  gettimeofday(&t_end2, NULL);
  gettimeofday(&t_end1, NULL);
  t_alloc +=
    (t_end2.tv_sec-t_beg2.tv_sec)*1000000+(t_end2.tv_usec-t_beg2.tv_usec);
  t_free =
    (t_end1.tv_sec-t_beg1.tv_sec)*1000000+(t_end1.tv_usec-t_beg1.tv_usec)-
    t_alloc;

  /* free remaining allocs */
  gettimeofday(&t_beg1, NULL);
  for (i=0; i<NUM_ALLOCS; ++i) {
    if (NULL != alloc[i]) {
      PFX(free)(alloc[i]);
      alloc[i] = NULL;
    }
  }
  gettimeofday(&t_end1, NULL);
  t_free +=
    (t_end1.tv_sec-t_beg1.tv_sec)*1000000+(t_end1.tv_usec-t_beg1.tv_usec);

  free(alloc);
  free(size);

  fprintf(stderr, "Time per malloc = %.2f us\n", t_alloc*1.0/NUM_ALLOCS);
  fprintf(stderr, "Time per free   = %.2f us\n", t_free*1.0/NUM_ALLOCS);

  return EXIT_SUCCESS;
}
