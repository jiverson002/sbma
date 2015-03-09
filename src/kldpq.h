/*
                An implementation of discrete priority queue
                    J. Iverson <jiverson@cs.umn.edu>
                                March 2015

  "Discrete priority queues" are a simple and efficient data structure
  for storing in sorted order, elements whose keys come from a "small"
  set of discrete values. Since the set of keys is small, the elements
  can be stored in bins corresponding to each of the discrete key
  values. The discrete priority queue structure supports adding of new
  elements, increasing / decreasing key values, and getting an element
  which has the minimum key value present in the structure.

  The code here is adapted from an auxillary data-structure near the top
  of page 587 of [1].

  The chief modification is that the discrete priority queue here allows
  the addition of new items into arbitrary bins. In other words, the
  bins need not be pre-initialized with all elements that will ever be
  included in the structure. It appears that this modification was
  unnecessary in [1], which is most likely why it was not described.

  [1] "A Linear-Time Algorithm for Finding a Sparse k-Connected Spanning
       Subgraph", Nagamochi and Ibaraki, Algorithmica, Springer-Verlag,
       1992, pp 583-596.
*/


#ifndef KLDPQ_H
#define KLDPQ_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint16_t */


/****************************************************************************/
/* Relevant types */
/****************************************************************************/
typedef uint16_t u16;


/****************************************************************************/
/* Macros to convert between size and bin number */
/****************************************************************************/
#define KLMAXBIN  378   /* zero indexed, so there are 379 bins */
#define KLMAXSIZE 65536 /* ... */

static u16 kl_bin2size[KLMAXBIN+1]=
{
  8, 16, 24, 32, 48, 56
};

static u16 kl_size2bin[KLMAXSIZE+1]=
{
 0, 0, 0, 0, 0
};

#define KLBIN2SIZE(B) ((B) >= 0 && (B) <=  KLMAXBIN ? kl_bin2size[(B)] : 0)
#define KLSIZE2BIN(S) ((S) >= 0 && (S) <= KLMAXSIZE ? kl_size2bin[(S)] : 0)


/****************************************************************************/
/* DPQ node */
/****************************************************************************/
typedef struct kl_dpq_node
{
  u16 bidx;               /* bin index */
  struct kl_dpq_node * p; /* previous node */
  struct kl_dpq_node * n; /* next node */
} kl_dpq_node_t;


/****************************************************************************/
/* DPQ bin */
/****************************************************************************/
typedef struct kl_dpq_bin
{
  struct kl_dpq_bin * p;   /* previous bin */
  struct kl_dpq_bin * n;   /* next bin */
  struct kl_dpq_node * hd; /* head node */
} kl_dpq_bin_t;


/****************************************************************************/
/* Discrete Priority Queue */
/****************************************************************************/
typedef struct kl_dpq
{
  struct kl_dpq_bin * hd;            /* head bin */
  struct kl_dpq_bin * tl;            /* tail bin */
  struct kl_dpq_bin bin[KLMAXBIN+1]; /* dpq bins */
} kl_dpq_t;


/****************************************************************************/
/* DPQ API */
/****************************************************************************/
#ifdef __cplusplus
extern "C"
{
#endif

int kl_dpq_init(kl_dpq_t * const dpq);
int kl_dpq_free(kl_dpq_t * const dpq);

int kl_dpq_ad(kl_dpq_t * const dpq, kl_dpq_node_t * const n);
int kl_dpq_rm(kl_dpq_t * const dpq, kl_dpq_node_t * const n);

int kl_dpq_move(kl_dpq_t * const dpq, kl_dpq_node_t * const n, int const bidx);

#ifdef __cplusplus
}
#endif

#endif
