#include <assert.h>

#include "kldpq.h"


/****************************************************************************/
/* Lookup tables to convert between size and bin number */
/****************************************************************************/
static u16 kl_bin2size[KLMAXBIN+1];
static int kl_size2bin[KLMAXSIZE+1];


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
  int i;

  kl_bin2size[0] = 8;
  for (i=1; i<=KLMAXBIN; ++i) {
    if (i <= 7)
      kl_bin2size[i] = kl_bin2size[i-1]+8;
    else if (i <  20)
      kl_bin2size[i] = kl_bin2size[i-1]+16;
    else if (i <  44)
      kl_bin2size[i] = kl_bin2size[i-1]+32;
    else if (i <  92)
      kl_bin2size[i] = kl_bin2size[i-1]+64;
    else if (i < 188)
      kl_bin2size[i] = kl_bin2size[i-1]+128;
    else if (i < 380)
      kl_bin2size[i] = kl_bin2size[i-1]+256;
  }

  kl_size2bin[0] = -1;
  for (i=1; i<=KLMAXSIZE; ++i) {
    if (i <= 64)
      kl_size2bin[i] = (i-1)/8;
    else if (i <=   256)
      kl_size2bin[i] = 8+(i-65)/16;
    else if (i <=  1024)
      kl_size2bin[i] = 20+(i-257)/32;
    else if (i <=  4096)
      kl_size2bin[i] = 44+(i-1025)/64;
    else if (i <= 16384)
      kl_size2bin[i] = 92+(i-4097)/128;
    else if (i <= 65536)
      kl_size2bin[i] = 188+(i-16385)/256;
  }

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


/****************************************************************************/
/* Find the node with the smallest size >= size parameter */
/****************************************************************************/
extern int
kl_dpq_find(kl_dpq_t * const dpq, size_t const size)
{
  int bidx;
  kl_dpq_bin_t * bin;

  bidx = KLSIZE2BIN(size);

  /* size is > threshold */
  if (-1 == bidx)
    return -1;

  bin = &(dpq->bin[bidx++]);

  /* if bin is inactive, check next bin */
  while (bidx <= KLMAXBIN && NULL == bin->p && NULL == bin->n)
    bin = &(dpq->bin[bidx++]);

  /* return the index of the bin with the smallest size >= size parameter */
  return bidx > KLMAXBIN ? -1 : bidx-1;
}


/****************************************************************************/
/* Main routine for testing */
/****************************************************************************/
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
