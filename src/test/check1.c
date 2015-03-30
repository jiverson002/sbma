#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "klmalloc.h"

/*#define SEED   1426830585*/
/*#define SEED   1426361468*/
/*#define SEED   1427208060*/
#define SEED   time(NULL)

/* probability to free a previous allocation */
#define PER_FREE      30

size_t NUM_ALLOCS = 1<<17;

int main(void)
{
  size_t i, j, k, l, sz;
  unsigned long seed;
  void ** alloc;
  size_t * size;

  seed = SEED;
  srand(seed);

  fprintf(stderr, "seed = %lu\n", seed);

  alloc = (void **) malloc(NUM_ALLOCS*sizeof(void *));
  assert(NULL != alloc);
  size = (size_t *) malloc(NUM_ALLOCS*sizeof(size_t));
  assert(NULL != size);

  for (i=0; i<NUM_ALLOCS; ++i) {
    j = rand()%100; /* indicator for big/med/sml alloc */
    k = rand()%100; /* indicator for free              */

    sz = j%(2*sizeof(void*)-1)+1;

    alloc[i] = KL_malloc(sz);
    assert(NULL != alloc[i]);

    if (k < PER_FREE) { /* 30% chance to free memory */
      if (0 == i)
        l = 0;
      else
        l = rand()%i;
      while (l < i && NULL == alloc[l])
        l++;

      if (NULL != alloc[l]) {
        KL_free(alloc[l]);
        alloc[l] = NULL;
      }
    }
  }

  /* free remaining allocs */
  for (i=0; i<NUM_ALLOCS; ++i) {
    if (NULL != alloc[i]) {
      KL_free(alloc[i]);
      alloc[i] = NULL;
    }
  }

  free(alloc);
  free(size);

  return EXIT_SUCCESS;
}
