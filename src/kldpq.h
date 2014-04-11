#ifndef KLDPQ_H
#define KLDPQ_H

#include <stdlib.h>
#include <string.h>

#include "kl.h"
#include "klutil.h"

#define KL_DPQ_SIZE (KL_PAGESIZE) /* this must be >= max entries in a block */

typedef struct kl_dpq_node {
  u32 k;
  u32 bid;
  struct kl_dpq_node * p, * n;
} kl_dpq_node_t;

typedef struct kl_dpq_bucket {
  kl_dpq_node_t * hd;
  struct kl_dpq_bucket * p, * n;
} kl_dpq_bucket_t;

typedef struct {
  size_t sz;
  size_t ssz;
  size_t tsz;
  size_t cap;
  size_t scap;
  u32    tlMax;
  kl_dpq_node_t   * mem;
  kl_dpq_node_t   ** smem;
  kl_dpq_node_t   * nxt;
  kl_dpq_bucket_t * hd, * tl;
  kl_dpq_bucket_t * bucket;
} kl_dpq_t;

void
kl_dpq_init(
  kl_dpq_t * const dpq,
  u32 const tlMax
);

void
kl_dpq_free(
  kl_dpq_t * const dpq
);

void
kl_dpq_reset(
  kl_dpq_t * const dpq
);

kl_dpq_node_t *
kl_dpq_new(
  kl_dpq_t * const dpq,
  u32 const bid
);

void
kl_dpq_del(
  kl_dpq_t * const dpq,
  kl_dpq_node_t * const n
);

void
kl_dpq_inc(
  kl_dpq_t * const dpq,
  kl_dpq_node_t * const n
);

void
kl_dpq_dec(
  kl_dpq_t * const dpq,
  kl_dpq_node_t * const n
);

kl_dpq_node_t *
kl_dpq_peek(
  kl_dpq_t const * const dpq
);

int
kl_dpq_empty(
  kl_dpq_t const * const dpq
);

kl_dpq_node_t *
kl_dpq_rpeek(
  kl_dpq_t const * const dpq
);

int
kl_dpq_rempty(
  kl_dpq_t const * const dpq
);

#endif
