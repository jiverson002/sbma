#include <assert.h>

#include "kldpq.h"


/****************************************************************************/
/* Internal node addition routine */
/****************************************************************************/
static int
kl_dpq_ad_int(kl_dpq_t * const dpq, kl_dpq_node_t * const n, u16 const bidx)
{
  /* prepend n to front of bin[bidx] linked-list */
  n->bidx = bidx;
  n->p    = NULL;
  n->n    = dpq->bin[bidx].hd;
  if (NULL != dpq->bin[bidx].hd)
    dpq->bin[bidx].hd->p = n;
  dpq->bin[bidx].hd = n;

  /* if list is empty, update head and tail of bin[bidx] */
  if (NULL == dpq->tl) {
    /* if dpq->tl is NULL, then dpq->hd must also be NULL */
    assert(NULL == dpq->hd);

    /* update dpq->hd as well */
    dpq->hd = &(dpq->bin[bidx]);
    dpq->tl = &(dpq->bin[bidx]);

    /* bin[bidx] must be the only active bin */
    assert(NULL == dpq->hd->p);
    assert(NULL == dpq->hd->n);
  }
  else if (dpq->tl != &(dpq->bin[bidx])) {
    /* since bin[bidx] is last valid bin and it is not the tail, it must not
     * be active */
    assert(NULL == dpq->bin[bidx].p);
    assert(NULL == dpq->bin[bidx].n);

    /* next pointer for dpq->tl must be invalid */
    assert(NULL == dpq->tl->n);

    /* insert bin[bidx] as tail */
    dpq->bin[bidx].p = dpq->tl;
    dpq->tl->n       = &(dpq->bin[bidx]);
    dpq->tl          = &(dpq->bin[bidx]);
  }

  return 0;
}


/****************************************************************************/
/* Initialize DPQ */
/****************************************************************************/
extern int
kl_dpq_init(kl_dpq_t * const dpq)
{
  u16 i;

  for (i=0; i<=KLMAXBIN; ++i) {
    dpq->bin[i].p  = NULL;
    dpq->bin[i].n  = NULL;
    dpq->bin[i].hd = NULL;
  }
  dpq->hd = NULL;
  dpq->tl = NULL;

  return 0;
}


/****************************************************************************/
/* Free DPQ */
/****************************************************************************/
extern int
kl_dpq_free(kl_dpq_t * const dpq)
{
  return 0;
}


/****************************************************************************/
/* Add a node to a DPQ */
/****************************************************************************/
extern int
kl_dpq_ad(kl_dpq_t * const dpq, kl_dpq_node_t * const n)
{
  return kl_dpq_ad_int(dpq, n, KLMAXBIN);
}


/****************************************************************************/
/* Remove a node from a DPQ */
/****************************************************************************/
extern int
kl_dpq_rm(kl_dpq_t * const dpq, kl_dpq_node_t * const n)
{
  u16 bidx = n->bidx;

  /* shift head of this bucket, three things to handle:
      1)   HEAD  -> NODE <-> *,      becomes   HEAD  -> *
      2) NODE-M <-> NODE <-> *,      becomes NODE-M  -> *
      3)      * <-> NODE <-> NODE+N, becomes      * <-> NODE+N
     where NODE-M != NULL and NODE+N !+ NULL, but * can be anything valid or
     NULL.
   */
  if (NULL == n->p) {
    /* since previous pointer is not valid, it must be the head of the list */
    assert(n == dpq->bin[bidx].hd);

    /* update head of list */
    dpq->bin[bidx].hd = n->n;
  }
  else {
    /* since n is not the head of the linked-list, it must have a valid
     * previous pointer */
    assert(NULL != n->p);

    /* update previous node's next pointer */
    n->p->n = n->n;
  }

  /* update next node's previous pointer, if next node is valid */
  if (NULL != n->n)
    n->n->p = n->p;


  /* check if bin[bidx] is empty, if so, remove it from the bin linked-list,
     four things to handle:
      1) DPQHEAD  -> BIN[BIDX] <-> *,       becomes DPQHEAD -> *
      2)  BIN[?] <-> BIN[BIDX] <-> *,       becomes BIN[?] <-> *
      3)       * <-> BIN[BIDX] <-  DPQTAIL, becomes * <-> DPQTAIL
      4)       * <-> BIN[BIDX] <-> BIN[?],  becomes * <-> BIN[?]
   */
  if (NULL == dpq->bin[bidx].hd) {
    if (NULL == dpq->bin[bidx].p) {
      assert(dpq->hd == &(dpq->bin[bidx]));
      dpq->hd = dpq->bin[bidx].n;
    }
    else {
      assert(dpq->hd != &(dpq->bin[bidx]));
      dpq->bin[bidx].p->n = dpq->bin[bidx].n;
    }
    if (NULL == dpq->bin[bidx].n) {
      assert(dpq->tl == &(dpq->bin[bidx]));
      dpq->tl = dpq->bin[bidx].p;
    }
    else {
      assert(dpq->tl != &(dpq->bin[bidx]));
      dpq->bin[bidx].n->p = dpq->bin[bidx].p;
    }
  }

  return 0;
}


/****************************************************************************/
/* Move a node to a different bin */
/****************************************************************************/
extern int
kl_dpq_move(kl_dpq_t * const dpq, kl_dpq_node_t * const n, int const bidx)
{
  int ret;
  if (0 != (ret=kl_dpq_rm(dpq, n)))
    return ret;
  return kl_dpq_ad_int(dpq, n, bidx);
}


#if 0
/****************************************************************************/
/* Increment a node */
/****************************************************************************/
extern void
kl_dpq_incr(kl_dpq_t * const dpq, kl_dpq_node_t * const n)
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


/****************************************************************************/
/* Decrement a node */
/****************************************************************************/
extern void
kl_dpq_decr(kl_dpq_t * const dpq, kl_dpq_node_t * const n)
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
#endif


#include <stdlib.h>

int main(void)
{
  int i, N=128;
  kl_dpq_t dpq;
  kl_dpq_node_t * n;

  n = malloc(N*sizeof(kl_dpq_node_t));
  assert(NULL != n);

  kl_dpq_init(&dpq);

  for (i=0; i<N; ++i)
    kl_dpq_ad(&dpq, n+i);

  for (i=0; i<N; ++i)
    kl_dpq_move(&dpq, n+i, i%KLMAXBIN);

  for (i=0; i<N; ++i)
    kl_dpq_rm(&dpq, n+i);

  kl_dpq_free(&dpq);

  free(n);

  return EXIT_SUCCESS;
}
