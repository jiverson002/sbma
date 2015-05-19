#define _GNU_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "vmm.h"

static int init=0;
static struct vmm vmm;
#ifdef USE_PTHREAD
static pthread_mutex_t init_lock=PTHREAD_MUTEX_INITIALIZER;
#endif


/****************************************************************************/
/* Ensure that the vmm has been initialized. */
/****************************************************************************/
#define __vmm_init_check__()\
((\
  0 == LOCK_GET(&init_lock) &&\
  (0 != init ||\
    __vmm_init__(&vmm, mmap_opts[MMAP_PAGE_SIZE]*sysconf(_SC_PAGESIZE),\
      262144, mmap_opts[MMAP_LAZY_READ])) &&\
  0 == LOCK_LET(&init_lock)\
) ? 0 : -1)


int main()
{
  return EXIT_SUCCESS;
}
