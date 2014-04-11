/*
                An implementation of top-down splaying
                    D. Sleator <sleator@cs.cmu.edu>
                           March 1992

  "Splay trees", or "self-adjusting search trees" are a simple and
  efficient data structure for storing an ordered set.  The data
  structure consists of a binary tree, without parent pointers, and no
  additional fields.  It allows searching, insertion, deletion,
  deletemin, deletemax, splitting, joining, and many other operations,
  all with amortized logarithmic performance.  Since the trees adapt to
  the sequence of requests, their performance on real access patterns is
  typically even better.  Splay trees are described in a number of texts
  and papers [1,2,3,4,5].

  The code here is adapted from simple top-down splay, at the bottom of
  page 669 of [3].  It can be obtained via anonymous ftp from
  spade.pc.cs.cmu.edu in directory /usr/sleator/public.

  The chief modification here is that the splay operation works even if the
  item being splayed is not in the tree, and even if the tree root of the
  tree is NULL.  So the line:

                              t = splay(i, t);

  causes it to search for item with key i in the tree rooted at t.  If it's
  there, it is splayed to the root.  If it isn't there, then the node put
  at the root is the last one before NULL that would have been reached in a
  normal binary search for i.  (It's a neighbor of i in the tree.)  This
  allows many other operations to be easily implemented, as shown below.

  [1] "Fundamentals of data structures in C", Horowitz, Sahni,
       and Anderson-Freed, Computer Science Press, pp 542-547.
  [2] "Data Structures and Their Algorithms", Lewis and Denenberg,
       Harper Collins, 1991, pp 243-251.
  [3] "Self-adjusting Binary Search Trees" Sleator and Tarjan,
       JACM Volume 32, No 3, July 1985, pp 652-686.
  [4] "Data Structure and Algorithm Analysis", Mark Weiss,
       Benjamin Cummins, 1992, pp 119-130.
  [5] "Data Structures, Algorithms, and Performance", Derick Wood,
       Addison-Wesley, 1993, pp 367-375.
*/

#include "klsplay.h"

static void
kl_splay_grow(
  kl_splay_tree_t * const sp  /* kl splay tree */
)
{
  void * tmp;

  if (sp->ssz >= sp->scap) {
    tmp = KL_MMAP(sp->scap*2*sizeof(kl_splay_t *));
    memcpy(tmp, sp->smem, sp->scap*sizeof(kl_splay_t *));
    KL_MUNMAP(sp->smem, sp->scap*sizeof(kl_splay_t *));
    sp->scap *= 2;
    sp->smem  = (kl_splay_t **) tmp;
  }

  tmp = KL_MMAP(sp->cap*sizeof(kl_splay_t));
  sp->mem = sp->smem[sp->ssz++] = (kl_splay_t *) tmp;
  sp->sz  = 0;
}

static kl_splay_t *
kl_splay_new(
  kl_splay_tree_t * const sp, /* kl splay tree */
  uptr const blk,             /* ptr to block  */
  u32 const pid,              /* pool id       */
  u32 const bid               /* block id      */
)
{
  kl_splay_t * n;

  if (sp->sz >= sp->cap) {
    kl_splay_grow(sp);
  }

  n = sp->mem+sp->sz;
  n->l = NULL;
  n->r = NULL;
  n->k = blk;
  n->p = pid;
  n->b = bid;
  sp->sz++;

  return n;
}

static kl_splay_t *
kl_splay_splay(
  uptr const blk,
  kl_splay_t * t
)
{
  kl_splay_t N, * l, * r, * y;

  if (t == NULL) {
    return t;
  }
  N.l = N.r = NULL;
  l = r = &N;

  for (;;) {
    if (blk < t->k) {
      if (t->l == NULL) break;
      if (blk < t->l->k) {
        y = t->l;                         /* rotate right */
        t->l = y->r;
        y->r = t;
        t = y;
        if (t->l == NULL) break;
      }
      r->l = t;                           /* link right */
      r = t;
      t = t->l;
    }else if (blk > t->k) {
      if (t->r == NULL) break;
      if (blk > t->r->k) {
        y = t->r;                         /* rotate left */
        t->r = y->l;
        y->l = t;
        t = y;
        if (t->r == NULL) break;
      }
      l->r = t;                           /* link left */
      l = t;
      t = t->r;
    }else {
      break;
    }
  }
  l->r = t->l;                            /* assemble */
  r->l = t->r;
  t->l = N.r;
  t->r = N.l;

  return t;
}

void
kl_splay_init(
  kl_splay_tree_t * const sp
)
{
  sp->sz  = 0;
  sp->ssz = 0;
  sp->cap   = KL_PAGESIZE/sizeof(kl_splay_t);
  sp->scap  = KL_PAGESIZE/sizeof(kl_splay_t *);
  sp->root  = NULL;
  sp->mem   = (kl_splay_t *) KL_MMAP(sp->cap*sizeof(kl_splay_t));
  sp->smem  = (kl_splay_t **) KL_MMAP(sp->scap*sizeof(kl_splay_t *));
  sp->smem[sp->ssz++] = sp->mem;
}

void
kl_splay_free(
  kl_splay_tree_t * const sp
)
{
  size_t i;
  for (i=0; i<sp->ssz; ++i) {
    KL_MUNMAP(sp->smem[i], sp->cap*sizeof(kl_splay_t));
  }
  KL_MUNMAP(sp->smem, sp->scap*sizeof(kl_splay_t *));
  sp->sz   = 0;
  sp->ssz  = 0;
  sp->cap  = 0;
  sp->scap = 0;
}

int
kl_splay_insert(
  kl_splay_tree_t * const sp,
  uptr const blk,
  u32 const pid,
  u32 const bid
)
{
  kl_splay_t * z, * t=sp->root;

  z = kl_splay_new(sp, blk, pid, bid); 
  if (t == NULL) {
    sp->root = z;
    return 1;
  }
  t = kl_splay_splay(blk, t);
  if (blk < t->k) {
    z->l = t->l;
    z->r = t;
    t->l = NULL;
    sp->root = z;
    return 1;
  }else if (blk > t->k) {
    z->r = t->r;
    z->l = t;
    t->r = NULL;
    sp->root = z;
    return 1;
  }else {           /* We get here if it's already in the tree. Don't add it */
    sp->sz--;       /* again, just overwrite the existing values.            */
    t->p     = pid;
    t->b     = bid;
    sp->root = t;
    return 0;
  }
}

int
kl_splay_find(
  kl_splay_tree_t * const sp,
  uptr const blk,
  u32 * const pid,
  u32 * const bid
)
{
  /* splay n to root of tree */
  sp->root = kl_splay_splay(blk, sp->root);

  /* if root equals n, then n exists in tree and can be returned. if not, then
   * n does not exist is tree and nothing will be returned, but a neighbor of
   * where n would be in the tree will be made root and the tree will be
   * slightly more balanced. */
  if(sp->root && blk == sp->root->k) {
    *pid = sp->root->p;
    *bid = sp->root->b;
    return 1;
  }
  return 0;
}

void
kl_splay_delete(
  kl_splay_tree_t * const sp,
  uptr const blk
)
{
  /* Deletes item from the tree if it's there.            */
  /* Set root of sp to the resulting tree.                */
  /* Does not release any memory for reuse.               */
  kl_splay_t * x, * t=sp->root;

  if (t == NULL) {
    sp->root = NULL;
    return;
  }
  t = kl_splay_splay(blk, t);
  if (blk == t->k) {           /* found it */
    if (t->l == NULL) {
      x = t->r;
    }else {
      x = kl_splay_splay(blk, t->l);
      x->r = t->r;
    }
    /*free(t);*/
    sp->root = x;
    return;
  }
  sp->root = t;
  return;                         /* It wasn't there */
}
