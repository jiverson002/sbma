#define _GNU_SOURCE
#define _POSIX_C_SOURCE 199309L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "mmu.h"
#include "vmm.h"

int main()
{
  struct mmu mmu;

  __mmu_init__(&mmu, 16384, 262144);

  __mmu_new_ate__(&mmu, (void*)17463945, 1024, MMU_RSDNT|MMU_ZFILL);
  //__mmu_set_flag__(&mmu, (void*)17463945, MMU_DIRTY);

  //assert(15 == __mmu_get_flag__(&mmu, (void*)17463945));
  //assert( 1 == __mmu_get_field__(__mmu_get_flag__(&mmu, (void*)17463945), MMU_DIRTY));
  //assert( 1 == __mmu_get_field__(__mmu_get_flag__(&mmu, (void*)17463945), MMU_ALLOC));

  //__mmu_set_flags__(&mmu, (void*)17463945, 1024, MMU_RSDNT|MMU_ZFILL);

  //__mmu_unset_flag__(&mmu, (void*)17463945, MMU_ZFILL);

  //assert(13 == __mmu_get_flag__(&mmu, (void*)17463945));
  //assert( 1 == __mmu_get_field__(__mmu_get_flag__(&mmu, (void*)17463945), MMU_DIRTY));
  //assert( 1 == __mmu_get_field__(__mmu_get_flag__(&mmu, (void*)17463945), MMU_ALLOC));

  __mmu_del_ate__(&mmu, (void*)17463945, 1024);

  //assert( 0 == __mmu_get_flag__(&mmu, (void*)17463945));
  //assert( 0 == __mmu_get_field__(__mmu_get_flag__(&mmu, (void*)17463945), MMU_DIRTY));
  //assert( 0 == __mmu_get_field__(__mmu_get_flag__(&mmu, (void*)17463945), MMU_ALLOC));

  __mmu_destroy__(&mmu);

  return EXIT_SUCCESS;
}
