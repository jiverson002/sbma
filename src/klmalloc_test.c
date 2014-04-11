#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "klmalloc.h"
#include "klmallinfo.h"

/* probability for each type of allocation (must sum to 100) */
#define PER_BIG_ALLOC 5
#define PER_MED_ALLOC 35
#define PER_SML_ALLOC 60

/* probability to free a previous allocation */
#define PER_FREE      30

size_t NUM_ALLOCS     = 1<<17;
/*size_t NUM_ALLOCS     = 1<<12;*/
size_t BIG_ALLOC_SIZE = 1<<25; /* 16MB */
size_t MED_ALLOC_SIZE = 1<<15; /* 16KB */
size_t SML_ALLOC_SIZE = 1<<11; /* 1KB  */

size_t
getvmem()
{
  int                pid, ppid, pgrp, session, tty_nr, tpgid;
  char               state;
  unsigned           flags;
  long int           cutime, cstime, priority, nice, num_threads, itrealvalue;
  long unsigned      minflt, cminflt, majflt, cmajflt, utime, stime, vsize;
  /*long */long unsigned starttime;
  char               comm[1024];

  FILE * f = fopen("/proc/self/stat", "r");
  
  if(fscanf(f, "%d %s %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld "
               "%ld %ld %ld %ld %lu %lu", &pid, comm, &state, &ppid, &pgrp,
                &session, &tty_nr, &tpgid, &flags, &minflt, &cminflt, &majflt,
                &cmajflt, &utime, &stime, &cutime, &cstime, &priority, &nice,
                &num_threads, &itrealvalue, &starttime, &vsize) != 23) {}
  fclose(f);

  return vsize;
}

int
main()
{
  size_t i, j, k, l, sz, vmem, mxvmem;
  unsigned long ta, tf, seed;
  void * buf;
  void ** alloc;
  struct timeval ts, te;

  seed = time(NULL);
  /*seed = 1395813969;*/ /* getting wierd results for this */
  srand(seed);

  fprintf(stderr, "seed:   %lu\n", seed);

  ta     = 0;
  tf     = 0;
  mxvmem = 0;
  alloc  = (void **) malloc(NUM_ALLOCS*sizeof(void *));
  buf    = malloc(BIG_ALLOC_SIZE);

  for (i=0; i<NUM_ALLOCS; ++i) {
    j = rand()%100; /* indicator for big/med/sml alloc */
    k = rand()%100; /* indicator for free              */

    if (j < PER_BIG_ALLOC) {        /* 5% chance to make big alloc  */
      sz = rand()%BIG_ALLOC_SIZE;
    }else if (j < PER_MED_ALLOC) {  /* 35% chance to make med alloc */
      sz = rand()%MED_ALLOC_SIZE;
    }else {                         /* 60% chance to make sml alloc */
      sz = rand()%SML_ALLOC_SIZE;
    }
    sz++;
    gettimeofday(&ts, NULL);
    alloc[i] = malloc(sz);
    gettimeofday(&te, NULL);
    ta += (te.tv_sec-ts.tv_sec)*1000000 + te.tv_usec-ts.tv_usec;

    /* make sure we can read from allocated memory */
    memcpy(buf, alloc[i], sz);

    if (k < PER_FREE) { /* 30% chance to free memory */
      if (!i) {
        l = 0;
      }else {
        l = rand()%i;
      }
      while (l < i && !alloc[l]) {
        l++;
      }
      if (alloc[l]) {
        gettimeofday(&ts, NULL);
        free(alloc[l]);
        gettimeofday(&te, NULL);
        tf += (te.tv_sec-ts.tv_sec)*1000000 + te.tv_usec-ts.tv_usec;
        alloc[l] = NULL;
      }else {
        l = 0;
        while (l < i && !alloc[l]) {
          l++;
        }
        gettimeofday(&ts, NULL);
        free(alloc[l]);
        gettimeofday(&te, NULL);
        tf += (te.tv_sec-ts.tv_sec)*1000000 + te.tv_usec-ts.tv_usec;
        alloc[l] = NULL;
      }
    }

    vmem = getvmem();
    mxvmem = vmem > mxvmem ? vmem : mxvmem;
  }

#if defined(KL_MALLOC)
  klstats();
#endif

  /* free remaining allocs */
  for (i=0; i<NUM_ALLOCS; ++i) {
    if (alloc[i]) {
      gettimeofday(&ts, NULL);
      free(alloc[i]);
      gettimeofday(&te, NULL);
      tf += (te.tv_sec-ts.tv_sec)*1000000 + te.tv_usec-ts.tv_usec;
      alloc[i] = NULL;
    }
  }

  fprintf(stderr, "malloc: %.2f us\n", ta*1.0/NUM_ALLOCS);
  fprintf(stderr, "free:   %.2f us\n", tf*1.0/NUM_ALLOCS);
  fprintf(stderr, "vmem:   %.2f mB\n", mxvmem/1000000.0);

  free(alloc);
  free(buf);

  return EXIT_SUCCESS;
}
