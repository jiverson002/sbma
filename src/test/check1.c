#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "klmalloc.h"

/*#define SEED 1426830585*/
/*#define SEED 1426361468*/
/*#define SEED 1427744642*/
#define SEED time(NULL)

/* probability to free a previous allocation */
#define PER_FREE 30

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
# define PFX(F) KL_##F
#endif

#define MALLOC 0
#define FREE   1

size_t NUM_OPS = 1<<25;
size_t MAX_OPS = 1<<25;

/*
  awk '{if (3 == NF) {map[$2]=ctr++; print $1,$3} else if (2 == NF)
  {print $1,map[$2];}}' malloc.out > malloc.map
 */

int main(int argc, char * argv[])
{
  char c;
  size_t i, j, k, l, sz, na=0, nf=0;
  unsigned long t_alloc=0, t_free=0, seed;
  struct timeval t_beg1, t_beg2, t_end1, t_end2;
  void ** alloc;
  char * op, * file=NULL;
  size_t * oprnd, * size;
  FILE * fp;

  switch (argc) {
    case 3:
      MAX_OPS = atol(argv[2]);
    case 2:
      file = argv[1];
  }

  KL_mallopt(M_ENABLED, M_ENABLED_ON);

  op = (char *) PFX(malloc)(MAX_OPS*sizeof(char));
  assert(NULL != op);
  oprnd = (size_t *) PFX(malloc)(MAX_OPS*sizeof(size_t));
  assert(NULL != oprnd);
  size = (size_t *) PFX(malloc)(MAX_OPS*sizeof(size_t));
  assert(NULL != size);
  alloc = (void **) PFX(malloc)(MAX_OPS*sizeof(void *));
  assert(NULL != alloc);

  if (NULL != file) {
    fp = fopen(argv[1], "r");
    assert(NULL != fp);

    for (NUM_OPS=0; NUM_OPS<MAX_OPS; ++NUM_OPS) {
      switch (fscanf(fp, "%c %zu %zu\n", &c, &i, &j)) {
        case EOF:
          goto DONE;

        default:
          if ('m' == c) {
            oprnd[NUM_OPS] = i;
            size[NUM_OPS]  = j;
            op[NUM_OPS]    = MALLOC;
            na++;
          }
          else if ('f' == c) {
            oprnd[NUM_OPS] = i;
            op[NUM_OPS] = FREE;
            nf++;
          }
          break;
      }
    }
    DONE:
    fclose(fp);
  }
  else {
    seed = SEED;
    srand(seed);

    fprintf(stderr, "seed = %lu\n", seed);

    na = NUM_OPS;
    nf = NUM_OPS;
  }

  gettimeofday(&t_beg1, NULL);
  gettimeofday(&t_beg2, NULL);
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != file) {
      if (MALLOC == op[i]) {
        sz = size[i];

        alloc[oprnd[i]] = PFX(malloc)(sz);
        assert(NULL != alloc[oprnd[i]]);

        memset(alloc[oprnd[i]], 1, sz);
      }
      else if (FREE == op[i]) {
        gettimeofday(&t_end2, NULL);
        t_alloc +=
          (t_end2.tv_sec-t_beg2.tv_sec)*1000000+(t_end2.tv_usec-t_beg2.tv_usec);

        PFX(free)(alloc[oprnd[i]]);
        alloc[oprnd[i]] = NULL;

        gettimeofday(&t_beg2, NULL);
      }
    }
    else {
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
  for (i=0; i<NUM_OPS; ++i) {
    if (NULL != alloc[i]) {
      PFX(free)(alloc[i]);
      alloc[i] = NULL;
    }
  }
  gettimeofday(&t_end1, NULL);
  t_free +=
    (t_end1.tv_sec-t_beg1.tv_sec)*1000000+(t_end1.tv_usec-t_beg1.tv_usec);

  PFX(free)(op);
  PFX(free)(oprnd);
  PFX(free)(size);
  PFX(free)(alloc);

  fprintf(stderr, "%zu %zu\n", na, nf);
  fprintf(stderr, "Time per malloc = %.2f us\n", t_alloc*1.0/na);
  fprintf(stderr, "Time per free   = %.2f us\n", t_free*1.0/nf);

  return EXIT_SUCCESS;
}
