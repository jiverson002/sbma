/*
                An implementation of discrete priority queue
                    J. Iverson <jiverson@cs.umn.edu>
                                March 2014

  "Discrete priority queues" are a simple and efficient data structure
  for storing in sorted order, elements whose keys come from a "small"
  set of discrete values. Since the set of keys is small, the elements
  can be stored in buckets corresponding to each of the discrete key
  values. The discrete priority queue structure supports adding of new
  elements, increasing / decreasing key values, and getting an element 
  which has the minimum key value present in the structure.

  The code here is adapted from an auxillary data-structure near the top
  of page 587 of [1].

  The chief modification here is that the discrete priority queue here
  allows the addition of new items into bucket 0. In other words, the
  structure need not be pre-initialized with all elements that will 
  ever be included in the structure. It appears that this modification
  was unnecessary in [1], which is most likely why it was not described.

  [1] "A Linear-Time Algorithm for Finding a Sparse k-Connected Spanning
       Subgraph", Nagamochi and Ibaraki, Algorithmica, Springer-Verlag,
       1992, pp 583-596.
*/

#include "kldpq.h"

static void
kl_dpq_grow(
  kl_dpq_t * const dpq
)
{
  void * tmp;

  if (dpq->ssz >= dpq->scap) {
    tmp = KL_MMAP(dpq->scap*2*sizeof(kl_dpq_node_t *));
    memcpy(tmp, dpq->smem, dpq->scap*sizeof(kl_dpq_node_t *));
    KL_MUNMAP(dpq->smem, dpq->scap*sizeof(kl_dpq_node_t *));
    dpq->scap *= 2;
    dpq->smem  = (kl_dpq_node_t **) tmp;
  }

  tmp = KL_MMAP(dpq->cap*sizeof(kl_dpq_node_t));
  dpq->mem = dpq->smem[dpq->ssz++] = (kl_dpq_node_t *) tmp;
  dpq->tsz++;
  dpq->sz  = 0;
}

void
kl_dpq_init(
  kl_dpq_t * const dpq,
  u32 const tlMax
)
{
  u32 i;
  dpq->bucket = 
    (kl_dpq_bucket_t *) KL_MMAP(KL_DPQ_SIZE*sizeof(kl_dpq_bucket_t));
  for (i=0; i<KL_DPQ_SIZE; ++i) {
    dpq->bucket[i].hd = NULL;
    dpq->bucket[i].p  = NULL;
    dpq->bucket[i].n  = NULL;
  }
  dpq->hd    = NULL;
  dpq->tl    = NULL;
  dpq->tlMax = tlMax;
  dpq->sz    = 0;
  dpq->ssz   = 0;
  dpq->tsz   = 0;
  dpq->cap   = KL_PAGESIZE/sizeof(kl_dpq_node_t);
  dpq->scap  = KL_PAGESIZE/sizeof(kl_dpq_node_t *);
  dpq->mem   = (kl_dpq_node_t *) KL_MMAP(dpq->cap*sizeof(kl_dpq_node_t));
  dpq->smem  = (kl_dpq_node_t **) KL_MMAP(dpq->scap*sizeof(kl_dpq_node_t *));
  dpq->smem[dpq->ssz++] = dpq->mem;
  dpq->tsz++;
}

void
kl_dpq_free(
  kl_dpq_t * const dpq
)
{
  u32 i;
  for (i=0; i<dpq->tsz; ++i) {
    KL_MUNMAP(dpq->smem[i], dpq->cap*sizeof(kl_dpq_node_t));
  }
  KL_MUNMAP(dpq->smem, dpq->scap*sizeof(kl_dpq_node_t *));
  KL_MUNMAP(dpq->bucket, KL_DPQ_SIZE*sizeof(kl_dpq_bucket_t));
  dpq->sz   = 0;
  dpq->ssz  = 0;
  dpq->tsz  = 0;
  dpq->cap  = 0;
  dpq->scap = 0;
}

void
kl_dpq_reset(
  kl_dpq_t * const dpq
)
{
  u32 i;
  for (i=0; i<KL_DPQ_SIZE; ++i) {
    dpq->bucket[i].hd = NULL;
    dpq->bucket[i].p  = NULL;
    dpq->bucket[i].n  = NULL;
  }
  dpq->hd  = NULL;
  dpq->tl  = NULL;
  dpq->sz  = 0;
  dpq->ssz = 0;
  dpq->mem = dpq->smem[dpq->ssz++];
}

kl_dpq_node_t *
kl_dpq_new(
  kl_dpq_t * const dpq,
  u32 const bid
)
{
  kl_dpq_node_t * n;
  if (dpq->sz >= dpq->cap) {
    kl_dpq_grow(dpq);
  }
  n      = &dpq->mem[dpq->sz++];
  n->k   = 0;
  n->bid = bid;
  n->p   = NULL;
  n->n   = dpq->bucket[0].hd;
  if (dpq->bucket[0].hd) {
    dpq->bucket[0].hd->p = n;
  }
  dpq->bucket[0].hd = n;

  /* if dpq->hd does not already point to bucket[0], make it so. if dpq->hd is
   * NULL, then dpq->tl must also be NULL, so update it as well. */
  if (!dpq->hd) {
    dpq->hd    = dpq->bucket;
    dpq->tl    = dpq->bucket;
    dpq->hd->p = NULL;
    dpq->hd->n = NULL;
  }else if (dpq->hd != dpq->bucket) {
    dpq->bucket[0].p = NULL;
    dpq->bucket[0].n = dpq->hd;
    dpq->hd->p       = dpq->bucket;
    dpq->hd          = dpq->bucket;
  }

  return n;
}

void
kl_dpq_del(
  kl_dpq_t * const dpq,
  kl_dpq_node_t * const n
)
{
  u32 k = n->k;

  /* shift head of this bucket three cases to handle:
      1) NODE-M <-> NODE <-> *, becomes NODE-M -> NULL
      2) * <-> NODE <-> NODE+N, becomes * <-> NODE+N
      3) HEAD -> NODE <-> *, becomes HEAD -> *
     where NODE-M != NULL and NODE+N !+ NULL, but * can be anything valid or
     NULL.
   */
  if (dpq->bucket[k].hd == n) {
    dpq->bucket[k].hd = n->n;
  }else {
    n->p->n = n->n;
  }
  if (n->n) {
    n->n->p = n->p;
  }

  /* check if this bucket is empty, if so, remove it from the bucket
     configuration, handled by one of the following four cases:
      1) DPQHEAD ?? K
        IS)  DPQHEAD -> K+1
        NOT) K-M <-> K *, becomes K-M <-> *
      2) DPQTAIL ?? K
        IS)  K-M <- DPQTAIL
        NOT) * <-> K <-> K+N, becomes * <-> K+N
   */
  if (!dpq->bucket[k].hd) {
    if (dpq->hd == dpq->bucket+k) {
      dpq->hd = dpq->bucket[k].n;
      if (dpq->hd) {
        dpq->hd->p = NULL;
      }
    }else {
      dpq->bucket[k].p->n = dpq->bucket[k].n;
    }
    if (dpq->tl == dpq->bucket+k) {
      dpq->tl = dpq->bucket[k].p;
      if (dpq->tl) {
        dpq->tl->n = NULL;
      }
    }else {
      dpq->bucket[k].n->p = dpq->bucket[k].p;
    }
  }
}

void
kl_dpq_inc(
  kl_dpq_t * const dpq,
  kl_dpq_node_t * const n
)
{
  /* undefined if increase key past KL_DPQ_SIZE-1 */

  u32 k = n->k;

  /* there are two cases which need to be handled to ensure that the
     dpq->bucket gets configured correctly:
      1) * <-> K -> NULL, becomes * <-> K <-> K+1 -> NULL
      2) * <-> K <-> K+N, becomes * <-> K <-> K+1 <-> K+N
     where N != 1 and K+N != NULL, but * can be anything valid, even NULL.
   */
  if (!dpq->bucket[k].n) {
    dpq->bucket[k+1].p  = dpq->bucket+k;
    dpq->bucket[k+1].n  = NULL;

    dpq->bucket[k].n    = dpq->bucket+(k+1);

    dpq->tl             = dpq->bucket+(k+1);
  }else if (dpq->bucket[k].n != dpq->bucket+(k+1)) {
    dpq->bucket[k].n->p = dpq->bucket+(k+1);

    dpq->bucket[k+1].p  = dpq->bucket+k;
    dpq->bucket[k+1].n  = dpq->bucket[k].n;

    dpq->bucket[k].n    = dpq->bucket+(k+1);
  }

  /* shift head of this bucket three cases to handle:
      1) NODE-M <-> NODE <-> *, becomes NODE-M -> NULL
      2) * <-> NODE <-> NODE+N, becomes * <-> NODE+N
      3) HEAD -> NODE <-> *, becomes HEAD -> *
     where NODE-M != NULL and NODE+N != NULL, but * can be anything valid or
     NULL.
   */
  if (dpq->bucket[k].hd == n) {
    dpq->bucket[k].hd = n->n;
  }else {
    n->p->n = n->n;
  }
  if (n->n) {
    n->n->p = n->p;
  }

  /* check if this bucket is empty, if so, remove it from the bucket
     configuration, handled by one of the following three cases:
      1) DPQHEAD ?? K
        IS)  DPQHEAD -> K+1
        NOT) K-M <-> K *, becomes K-M <-> *
      2) * <-> K <-> K+N, becomes * <-> K+N
   */
  if (!dpq->bucket[k].hd) {
    if (dpq->hd == dpq->bucket+k) {
      dpq->hd = dpq->bucket[k].n;
      if (dpq->hd) {
        dpq->hd->p = NULL;
      }
    }else {
      dpq->bucket[k].p->n = dpq->bucket[k].n;
    }
    dpq->bucket[k].n->p = dpq->bucket[k].p;
  }

  /* update the node and make it head of the correct bucket */
  n->k = k+1;
  n->n = dpq->bucket[k+1].hd;
  if (n->n) {
    n->n->p = n;
  }
  dpq->bucket[k+1].hd = n;
}

void
kl_dpq_dec(
  kl_dpq_t * const dpq,
  kl_dpq_node_t * const n
)
{
  /* undefined if decrease key past 0 */

  u32 k = n->k;

  /* there are two cases which need to be handled to ensure that the
     dpq->bucket gets configured correctly:
      1) NULL <- K <-> *, becomes NULL <- K-1 <-> K <-> *
      2) K-M <-> K <-> *, becomes K-M <-> K-1 <-> K <-> *
     where M != 1 and K-M != NULL, but * can be anything valid, even NULL.
   */
  if (!dpq->bucket[k].p) {
    dpq->bucket[k-1].p  = NULL;
    dpq->bucket[k-1].n  = dpq->bucket+k;

    dpq->bucket[k].p    = dpq->bucket+(k-1);

    dpq->hd             = dpq->bucket+(k-1);
  }else if (dpq->bucket[k].p != dpq->bucket+(k-1)) {
    dpq->bucket[k].p->n = dpq->bucket+(k-1);

    dpq->bucket[k-1].p  = dpq->bucket[k].p;
    dpq->bucket[k-1].n  = dpq->bucket+k;

    dpq->bucket[k].p    = dpq->bucket+(k-1);
  }

  /* shift head of this bucket three cases to handle:
      1) NODE-M <-> NODE <-> *, becomes NODE-M -> NULL
      2) * <-> NODE <-> NODE+N, becomes * <-> NODE+N
      3) HEAD -> NODE <-> *, becomes HEAD -> *
     where NODE-M != NULL and NODE+N != NULL, but * can be anything valid or
     NULL.
   */
  if (dpq->bucket[k].hd == n) {
    dpq->bucket[k].hd = n->n;
  }else {
    n->p->n = n->n;
  }
  if (n->n) {
    n->n->p = n->p;
  }

  /* check if this bucket is empty, if so, remove it from the bucket
     configuration, handled by one of the following three cases:
      1) K-M <-> K *, becomes K-M <-> *
      2) DPQTAIL ?? K
        IS)  K-M <- DPQTAIL
        NOT) * <-> K <-> K+N, becomes * <-> K+N
   */
  if (!dpq->bucket[k].hd) {
    dpq->bucket[k].p->n = dpq->bucket[k].n;
    if (dpq->tl == dpq->bucket+k) {
      dpq->tl = dpq->bucket[k].p;
      if (dpq->tl) {
        dpq->tl->n = NULL;
      }
    }else {
      dpq->bucket[k].n->p = dpq->bucket[k].p;
    }
  }

  /* update the node and make it head of the correct bucket */
  n->k = k-1;
  n->n = dpq->bucket[k-1].hd;
  if (n->n) {
    n->n->p = n;
  }
  dpq->bucket[k-1].hd = n;
}

kl_dpq_node_t *
kl_dpq_peek(
  kl_dpq_t const * const dpq
)
{
  return dpq->hd->hd;
}

int
kl_dpq_empty(
  kl_dpq_t const * const dpq
)
{
  return dpq->hd == NULL;
}

kl_dpq_node_t *
kl_dpq_rpeek(
  kl_dpq_t const * const dpq
)
{
  /* if tl points to the last bucket (conceptually, the bucket which represents
   * a full block), then return the previous bucket, else return tl bucket */
  if (dpq->tl == dpq->bucket+(dpq->tlMax-1)) {
    return dpq->tl->p->hd;
  }else {
    return dpq->tl->hd;
  }
}

int
kl_dpq_rempty(
  kl_dpq_t const * const dpq
)
{
  if (dpq->tl == dpq->bucket+(dpq->tlMax-1)) {
    return dpq->tl->p == NULL;
  }else {
    return dpq->tl == NULL;
  }
}
