#ifndef KLSPLAY_H
#define KLSPLAY_H

#include <stdlib.h>
#include <string.h>

#include "kl.h"
#include "klutil.h"

typedef struct kl_splay {
  u32  p;                   /* pool id                 */
  u32  b;                   /* block id                */
  uptr k;                   /* ptr to block            */
  struct kl_splay * l, * r; /* left and right children */
} kl_splay_t;

typedef struct {
  size_t sz;
  size_t ssz;
  size_t cap;
  size_t scap;
  kl_splay_t * root;
  kl_splay_t * mem;
  kl_splay_t ** smem;
} kl_splay_tree_t;

void
kl_splay_init(
  kl_splay_tree_t * const sp
);

void
kl_splay_free(
  kl_splay_tree_t * const sp
);

int
kl_splay_insert(
  kl_splay_tree_t * const sp,
  uptr const blk,
  u32 const pid,
  u32 const bid
);

int
kl_splay_find(
  kl_splay_tree_t * const sp,
  uptr const blk,
  u32 * const pid,
  u32 * const bid
);

void
kl_splay_delete(
  kl_splay_tree_t * const sp,
  uptr const blk
);

#endif
