#ifndef KLHMAP_H
#define KLHMAP_H

#include <stdlib.h>
#include <string.h>

#include "kl.h"
#include "klutil.h"

#define KL_HMAP_SIZE (sizeof(size_t) + sizeof(u32) + sizeof(u32))

typedef struct {
  uptr msk;
  size_t sz, cap;
  void * mem;
} kl_hmap_t;

void
kl_hmap_init(
  kl_hmap_t * const hm
);

void
kl_hmap_free(
  kl_hmap_t * const hm
);

int
kl_hmap_insert(
  kl_hmap_t * const hm,
  uptr const blk,
  u32 const pid,
  u32 const bid
);

int
kl_hmap_find(
  kl_hmap_t * const sp,
  uptr const blk,
  u32 * const pid,
  u32 * const bid
);

void
kl_hmap_delete(
  kl_hmap_t * const sp,
  uptr const blk
);

#endif
