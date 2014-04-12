/*
                An implementation of dynamic memory allocator
                     J. Iverson <jiverson@cs.cmu.edu>
                       Wed Mar 26 00:31:59 CDT 2014

  Dynamic memory allocator which keeps pools of memory. Each pool has
  a fixed entry size, except for the last pool, which is a catch-all
  pool. In this pool, memory is allocated by request size and the size
  of the allocation is recorded as part of the allocation. The keys to
  this allocator are 1) page-sized block allocations in all memory pools
  except the last, which helps to reduce memory overhead, 2) splay-tree
  block maps, which allow for pointer lookup for memory free'ing in
  O(log n) time, and 3) discrete priority queues, which map blocks 
  within memory pools to allow for O(1) locating of blocks which have 
  free entries and can be (re-)allocated.
*/

/******************************************************************************
From Wikipedia
While the x86 architecture originally did not require aligned memory
access and still works without it, SSE2 instructions on x86 CPUs do
require the data to be 128-bit (16-byte) aligned and there can be
substantial performance advantages from using aligned data on these
architectures. However, there are also instructions for unaligned
access such as MOVDQU.

From C Standard
4.10.3 Memory management functions

   The order and contiguity of storage allocated by successive calls
to the calloc , malloc , and realloc functions is unspecified.  The
pointer returned if the allocation succeeds is suitably aligned so
that it may be assigned to a pointer to any type of object and then
used to access such an object in the space allocated (until the space
is explicitly freed or reallocated).  Each such allocation shall yield
a pointer to an object disjoint from any other object.  The pointer
returned points to the start (lowest byte address) of the allocated
space.  If the space cannot be allocated, a null pointer is returned.
If the size of the space requested is zero, the behavior is
implementation-defined; the value returned shall be either a null
pointer or a unique pointer.  The value of a pointer that refers to
freed space is indeterminate.


4.10.3.1 The calloc function

Synopsis

         #include <stdlib.h>
         void *calloc(size_t nmemb, size_t size);

Description

   The calloc function allocates space for an array of nmemb objects,
each of whose size is size .  The space is initialized to all bits
zero./114/

Returns

   The calloc function returns either a null pointer or a pointer to
the allocated space.


4.10.3.2 The free function


Synopsis

         #include <stdlib.h>
         void free(void *ptr);

Description

   The free function causes the space pointed to by ptr to be
deallocated, that is, made available for further allocation.  If ptr
is a null pointer, no action occurs.  Otherwise, if the argument does
not match a pointer earlier returned by the calloc , malloc , or
realloc function, or if the space has been deallocated by a call to
free or realloc , the behavior is undefined.

Returns

   The free function returns no value.  


4.10.3.3 The malloc function

Synopsis

         #include <stdlib.h>
         void *malloc(size_t size);

Description

   The malloc function allocates space for an object whose size is
specified by size and whose value is indeterminate.

Returns

   The malloc function returns either a null pointer or a pointer to
the allocated space.


4.10.3.4 The realloc function

Synopsis

         #include <stdlib.h>
         void *realloc(void *ptr, size_t size);

Description

   The realloc function changes the size of the object pointed to by
ptr to the size specified by size .  The contents of the object shall
be unchanged up to the lesser of the new and old sizes.  If the new
size is larger, the value of the newly allocated portion of the object
is indeterminate.  If ptr is a null pointer, the realloc function
behaves like the malloc function for the specified size.  Otherwise,
if ptr does not match a pointer earlier returned by the calloc ,
malloc , or realloc function, or if the space has been deallocated by
a call to the free or realloc function, the behavior is undefined.  If
the space cannot be allocated, the object pointed to by ptr is
unchanged.  If size is zero and ptr is not a null pointer, the object
it points to is freed.

Returns

   The realloc function returns either a null pointer or a pointer to
the possibly moved allocated space.
******************************************************************************/

/******************************************************************************
* TODO:
*   recode dpq so that free blocks are those returned which are almost full. it
*   the nearly empty blocks are always returned, then blocks will rarely become
*   empty and their memory will not be released.
*
*   recode this so that instead of block sizes being in powers of two, they are
*   in multiples of KL_PAGESIZE. this way, the memory can utilized with less
*   wasteage. what will this look like for blocks with size < KL_PAGESIZE?
*
* NOTE:
*   this code relies heavily on the fact that mmap returns page aligned memory
*   addresses. if this is not the case, then this code will have unspecified
*   results.
******************************************************************************/


#include "kl.h"
#include "kldpq.h"
#if defined(KL_WITH_HMAP)
  #include "klhmap.h"
#else
  #include "klsplay.h"
#endif
#include "klmallinfo.h"
#include "klutil.h"


/*****************************************************************************/
/* marco defs */
/*****************************************************************************/
#define MPOOL_INIT 17 /* must be <= 24 */
#define BPOOL_INIT (KL_PAGESIZE/sizeof(block_t))
#define BPOOL_TRSH 0.50


/*****************************************************************************/
/* marcos */
/*****************************************************************************/
#define MPOOL_INIT_CHECK {                                          \
  if (!mpool_is_init) {                                             \
    mpool_init();                                                   \
  }                                                                 \
}

#define B1         (1)
#define BSIZ       (sizeof(uchar)*CHAR_BIT)
#define bsiz(N)    ((N+BSIZ-1)/BSIZ)
#define bbyt(N)    (bsiz(N)*sizeof(uchar))
#define bget(B, I) (((B)[(I)/BSIZ])&(B1<<((I)%BSIZ)))
#define bset(B, I) (((B)[(I)/BSIZ])|=(B1<<((I)%BSIZ)))
#define buns(B, I) (((B)[(I)/BSIZ])&=~(B1<<((I)%BSIZ)))

#define KLFREE(PID, PTR, NUM)             \
  if(PID == MPOOL_INIT) {                 \
    (PTR) = (uchar *)((size_t *)(PTR)-1); \
    KL_MUNMAP(PTR, *((size_t *)(PTR)));     \
  }else {                                 \
    KL_MUNMAP(PTR, NUM);                  \
  }


/*****************************************************************************/
/* structs */
/*****************************************************************************/
typedef struct {
  u32     blockCtr; /* number of entries in block */
  uptr    blockID;  /* id of memory block         */
  uchar * blockPtr; /* pointer to block memory    */
} block_t;

typedef struct {
  size_t           poolSiz;  /* current block pool size          */
  size_t           poolAct;  /* current block pool active blocks */
  size_t           poolCap;  /* current block pool capacity      */
  block_t        * block;    /* array of blocks                  */
  kl_dpq_node_t  * cand;     /* pointer to candidate node        */
  kl_dpq_node_t ** blockMap; /* map from bid --> node(bid)       */
  kl_dpq_t         blockQ;   /* priority queue on blockCtr       */
} bpool_t;

typedef struct {
  size_t          poolCtr;             /* number of active allocations */
  bpool_t         bpool[MPOOL_INIT+1]; /* array of block pools         */
#ifdef KL_WITH_HMAP
  kl_hmap_t       blockMap;
#else
  kl_splay_tree_t blockMap;            /* map from blockID --> block   */
#endif
} mpool_t;


/*****************************************************************************/
/* extern variables */
/*****************************************************************************/
size_t KL_VMEM = 0, KL_MXVMEM = 0;


/*****************************************************************************/
/* static variables */
/*****************************************************************************/
static size_t KL_REQ = 0, KL_MXREQ = 0, KL_OVER = 0;
static mpool_t mpool;
static int mpool_is_init = 0;
/* size of block allocations (assume 4096 page size) */
static size_t asz_tbl[] = { 3640, 3854, 3968, 4032, 4064, 4064, 4032, 3968,
                            3840, 3584, 3072, 1<<12, 1<<13, 1<<14, 1<<15,
                            1<<16, 1<<17, 1<<18, 1<<19, 1<<20, 1<<21, 1<<22,
                            1<<23, 1<<24, 1<<25 };
/* number of entries per block */
static size_t num_tbl[] = { 3640, 1927, 992, 504, 254, 127, 63, 31, 15, 7, 3,
                            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };


/*****************************************************************************/
/* double size of block pool */
/*****************************************************************************/
static void
bpool_grow(
  bpool_t * const bp
)
{
  void * tmp;

  tmp = KL_MMAP(bp->poolCap*2*sizeof(block_t));
  memcpy(tmp, bp->block, bp->poolCap*sizeof(block_t));
  KL_MUNMAP(bp->block, bp->poolCap*sizeof(block_t));
  bp->block = (block_t *) tmp;

  tmp = KL_MMAP(bp->poolCap*2*sizeof(kl_dpq_node_t *));
  memcpy(tmp, bp->blockMap, bp->poolCap*sizeof(kl_dpq_node_t *));
  KL_MUNMAP(bp->blockMap, bp->poolCap*sizeof(kl_dpq_node_t *));
  bp->blockMap = (kl_dpq_node_t **) tmp;

  bp->poolCap *= 2;
}


/*****************************************************************************/
/* compact a block pool and release unused memory */
/*****************************************************************************/
static void
bpool_shrink(
  u32 const pid
)
{
  size_t    i, j;
  bpool_t * bp = mpool.bpool+pid;

  for (j=0, i=0; i<bp->poolSiz; ++i) {
    if (bp->blockMap[i]->k) {
#ifdef KL_WITH_HMAP
      kl_hmap_insert(&mpool.blockMap, (uptr)bp->block[i].blockPtr/KL_PAGESIZE,
                      pid, j);
#else
      /* the splay insert will overwrite the value it previously had */
      kl_splay_insert(&mpool.blockMap, (uptr)bp->block[i].blockPtr/KL_PAGESIZE,
                      pid, j);
#endif
      bp->blockMap[j] = bp->blockMap[i];
      bp->blockMap[j]->bid = j;
      bp->block[j++] = bp->block[i];
    }else {
      kl_dpq_del(&bp->blockQ, bp->blockMap[i]);
      bp->blockMap[i] = NULL;
      KLFREE(pid, bp->block[i].blockPtr, asz_tbl[pid]+bbyt(num_tbl[pid]));
    }
  }
  bp->poolSiz = j;
  if (!j) {
    bp->cand = NULL;
    kl_dpq_reset(&bp->blockQ);
  }else {
    /* pick the block which is least full, if any exists */
    bp->cand = kl_dpq_peek(&bp->blockQ);
  }
}


/*****************************************************************************/
/* initialize memory pool */
/*****************************************************************************/
static void
mpool_init()
{
  size_t i;

  /* give correct values to the large pool */
  asz_tbl[MPOOL_INIT] = 0;
  num_tbl[MPOOL_INIT] = 1;

  mpool.poolCtr = 0;
#ifdef KL_WITH_HMAP
  kl_hmap_init(&mpool.blockMap);
#else
  kl_splay_init(&mpool.blockMap);
#endif

  for (i=0; i<MPOOL_INIT; ++i) {
    mpool.bpool[i].poolAct = 0;
    mpool.bpool[i].poolSiz = 0;
    mpool.bpool[i].poolCap = BPOOL_INIT;
    mpool.bpool[i].block   = (block_t *) KL_MMAP(BPOOL_INIT*sizeof(block_t));
    mpool.bpool[i].cand     = NULL;
    mpool.bpool[i].blockMap =
      (kl_dpq_node_t **) KL_MMAP(BPOOL_INIT*sizeof(kl_dpq_node_t *));
    kl_dpq_init(&mpool.bpool[i].blockQ, num_tbl[i]);
  }
  /* initialize 'large' pool */
  mpool.bpool[i].poolAct = 0;
  mpool.bpool[i].poolSiz = 0;
  mpool.bpool[i].poolCap = BPOOL_INIT;
  mpool.bpool[i].block   = (block_t *) KL_MMAP(BPOOL_INIT*sizeof(block_t));
  mpool.bpool[i].cand     = NULL;
  mpool.bpool[i].blockMap =
    (kl_dpq_node_t **) KL_MMAP(BPOOL_INIT*sizeof(kl_dpq_node_t *));
  kl_dpq_init(&mpool.bpool[i].blockQ, num_tbl[i]);

  mpool_is_init = 1;
}


/*****************************************************************************/
/* destroy memory pool, free'ing memory */
/*****************************************************************************/
static void
mpool_free()
{
  size_t  i, j;
  bpool_t * bp;

  if (!mpool_is_init) {
    return;
  }

  for (i=0; i<MPOOL_INIT+1; ++i) {
    bp = mpool.bpool+i;

    for (j=0; j<bp->poolSiz; ++j) {
      if (bp->blockMap[j] != NULL) {
        /* has not been free'd in bpool_shrink */
        KLFREE(i, bp->block[j].blockPtr, asz_tbl[i]+bbyt(num_tbl[i]));
      }
    }
    KL_MUNMAP(bp->block, bp->poolCap*sizeof(block_t));
    KL_MUNMAP(bp->blockMap, bp->poolCap*sizeof(kl_dpq_node_t *));
    kl_dpq_free(&bp->blockQ);
    bp->poolSiz = 0;
    bp->poolCap = 0;
  }

#ifdef KL_WITH_HMAP
  kl_hmap_free(&mpool.blockMap);
#else
  kl_splay_free(&mpool.blockMap);
#endif
  mpool_is_init = 0;
}


/*****************************************************************************/
/* find block with free entry */
/*****************************************************************************/
static i32
bpool_find_free(
  u32 const pid,
  bpool_t * const bp
)
{
  if (bp->cand && bp->cand->k >= num_tbl[pid]) {  /* cand block is full      */
    bp->cand = NULL;
  }
#ifdef KL_DPQ_REV
  if(!bp->cand && !kl_dpq_rempty(&bp->blockQ)) {
    bp->cand = kl_dpq_rpeek(&bp->blockQ);
  }
#else
  if (!bp->cand && !kl_dpq_empty(&bp->blockQ)) { /* get next block from dpq */
    bp->cand = kl_dpq_peek(&bp->blockQ);
  }
#endif
  if (bp->cand) {                                /* return block if exists  */
    return bp->cand->bid;
  }
  return -1;                                     /* no free blocks exist    */
}


/*****************************************************************************/
/* find block id of a given ptr */
/*****************************************************************************/
static int
mpool_find_id(
  uptr const p,
  u32 * const pid,
  u32 * const bid
)
{
#ifdef KL_WITH_HMAP
  return kl_hmap_find(&mpool.blockMap, p/KL_PAGESIZE, pid, bid);
#else
  return kl_splay_find(&mpool.blockMap, p/KL_PAGESIZE, pid, bid);
#endif
}


/*****************************************************************************/
/* allocate memory block of sz bytes */
/*****************************************************************************/
void *
klmalloc(
  size_t const sz
)
{
  size_t    i, asz, esz, num;
  i32       bid;
  u32       pid;
  uchar   * ptr;
  bpool_t * bp;
  block_t * b;

  MPOOL_INIT_CHECK

  /* return NULL for zero sz allocations */
  if (!sz) return NULL;

  mpool.poolCtr++;
  KL_REQ += sz;
  KL_MXREQ = KL_REQ > KL_MXREQ ? KL_REQ : KL_MXREQ;

  /* calculate required info about request size */
  esz = kl_pow2up(sz);
  pid = kl_ilog2(esz);
  ptr = NULL;

  if (pid >= MPOOL_INIT) {
    /* all large entries go into same pool, bid is -1 so we will always 
     * allocate a new block */
    pid = MPOOL_INIT;
    bp  = mpool.bpool+pid;
    asz = sz;
    num = 1;
    bid = -1;
  }else {
    /* small entries must determine correct pool and block */
    bp  = mpool.bpool+pid;
    asz = asz_tbl[pid];
    num = num_tbl[pid];
    bid = bpool_find_free(pid, bp);
  }

  if (bid != -1) {
    /* find free entry in block f of block pool pid */
    b = bp->block+bid;

    if (!bp->blockMap[bid]->k) {
      /* reactivating this block, so increase active block counter */
      bp->poolAct++;
    }
    ptr = b->blockPtr+asz;
    for (i=0; i<num; ++i) { /* find free entry in a block which is known to
                               have free entries */
      if (!bget(ptr, i)) {
        KL_OVER += (esz - sz);
        bset(ptr, i);
        kl_dpq_inc(&bp->blockQ, bp->blockMap[bid]);
        return (void *) (b->blockPtr + (i*esz));
      }
    }
  }

  /* check size of memory pool */
  if (bp->poolSiz >= bp->poolCap) {
    bpool_grow(bp);
  }

  /* allocate new block */
  num = bbyt(num);
  if (pid == MPOOL_INIT) {
    KL_OVER += (kl_multup(sizeof(size_t) + asz + num, KL_PAGESIZE) - sz);
    ptr = KL_MMAP(sizeof(size_t) + asz + num);
    *((size_t *)ptr) = sizeof(size_t) + asz + num;
    ptr = (void *)((size_t *)ptr + 1);
  }else {
    KL_OVER += (esz - sz);
    ptr = KL_MMAP(asz + num);
  }
  memset(ptr+asz, 0, num);  /* unset all mark bits */
  bset(ptr+asz, 0);         /* mark the first bit  */

#ifdef KL_WITH_HMAP
  kl_hmap_insert(&mpool.blockMap, (uptr)(ptr)/KL_PAGESIZE, pid, bp->poolSiz);
#else
  kl_splay_insert(&mpool.blockMap, (uptr)(ptr)/KL_PAGESIZE, pid, bp->poolSiz);
#endif
  bp->blockMap[bp->poolSiz] = kl_dpq_new(&bp->blockQ, bp->poolSiz);
  kl_dpq_inc(&bp->blockQ, bp->blockMap[bp->poolSiz]);
  bp->block[bp->poolSiz].blockPtr = ptr;
  bp->poolSiz++;
  bp->poolAct++;

  return ptr;
}


/*****************************************************************************/
/* allocate and zero-initalize an array of num*size bytes */
/*****************************************************************************/
void *
klcalloc(
  size_t const num,
  size_t const size
)
{
  void * ptr = klmalloc(num*size);
  memset(ptr, 0, num*size);
  return ptr;
}


/*****************************************************************************/
/* deallocate memory block */
/*****************************************************************************/
void
klfree(
  void * const ptr
)
{
  size_t    asz, esz;
  u32       pid, bid, eid;
  uptr      p;
  bpool_t * bp;
  block_t * b;

  MPOOL_INIT_CHECK

  p   = (uptr) ptr;
  pid = 0;
  if (mpool_find_id(p, &pid, &bid)) {
    bp = mpool.bpool+pid;
    b  = bp->block+bid;
    
    esz = (1ul<<pid);
    asz = asz_tbl[pid];
    eid = (p-(uptr)b->blockPtr)/esz;

    KL_REQ -= ((pid == MPOOL_INIT) ? *((size_t *) b->blockPtr) : esz);

    buns(b->blockPtr+asz, eid);

    kl_dpq_dec(&bp->blockQ, bp->blockMap[bid]);
    if (!(bp->blockMap[bid]->k)) {
#ifdef KL_WITH_AGGRESSIVE
      /* release memory for block bid */
      kl_dpq_del(&bp->blockQ, bp->blockMap[bid]);
      bp->blockMap[bid] = NULL;
      KLFREE(pid, b->blockPtr, asz_tbl[pid]+bbyt(num_tbl[pid]));
      b->blockPtr = NULL;
      bp->cand = NULL;
#else
      bp->poolAct--;
      /* if the allocation was from the large pool, or the allocation was from
       * a pool whose current size is > 1 and whose active percent is less than
       * BPOOL_TRSH */
      if (pid == MPOOL_INIT) {
        kl_dpq_del(&bp->blockQ, bp->blockMap[bid]);
        bp->blockMap[bid] = NULL;
        KLFREE(pid, b->blockPtr, asz_tbl[pid]+bbyt(num_tbl[pid]));
        b->blockPtr = NULL;
      }else if (bp->poolSiz > 1 &&
        (double)bp->poolAct/bp->poolSiz <= BPOOL_TRSH)
      {
        bpool_shrink(pid);
      }
#endif
    }

    if (!(--mpool.poolCtr)) {
      mpool_free();
    }
  }
}


/*****************************************************************************/
/* resize memory block pointed to by ptr to be sz bytes */
/*****************************************************************************/
void *
klrealloc(
  void * const ptr,
  size_t const sz
)
{
  size_t   esz, besz;
  u32      pid, bid;
  void   * nptr;

  MPOOL_INIT_CHECK

  if (!ptr) {
    return klmalloc(sz);
  }

  if (mpool_find_id((uptr)ptr, &pid, &bid)) {
    if (pid == MPOOL_INIT) {
      esz  = sz;
      besz = *((size_t *)ptr - 1);
    }else {
      esz  = kl_pow2up(sz);
      besz = 1lu << pid;
    }

    if (esz == besz) {
      /* allocation can be left in same entry */
      return ptr;
    }

    nptr = klmalloc(sz);
    memcpy(nptr, ptr, (esz < besz ? esz : besz));
    klfree(ptr);
    return nptr;
  }

  /* ptr was invalid */
  return NULL;
}


/*****************************************************************************/
/* output imalloc structure statistics */
/*****************************************************************************/
struct klmallinfo
klmallinfo()
{
  unsigned long i, poolSiz=0, poolAct=0, poolCap=0, ovrhd=0;
  unsigned long dpqSiz=0, dpqCap=0;
  unsigned long splaySiz=0, splayCap=0;
  struct klmallinfo klmi;

  /* mpool */
  ovrhd    += sizeof(mpool_t);

  /* blockMap */
#ifdef KL_WITH_HMAP
#else
  splaySiz += mpool.blockMap.ssz;
  splayCap += mpool.blockMap.scap;
  ovrhd    += mpool.blockMap.ssz*mpool.blockMap.cap*sizeof(kl_splay_t);
  ovrhd    += mpool.blockMap.scap*sizeof(kl_splay_t *);
#endif

  for(i=0; i<MPOOL_INIT+1; ++i) {
    /* block */
    poolAct += mpool.bpool[i].poolAct;
    poolSiz += mpool.bpool[i].poolSiz;
    poolCap += mpool.bpool[i].poolCap;
    ovrhd   += mpool.bpool[i].poolCap*sizeof(block_t);

    /* blockMap */
    ovrhd   += mpool.bpool[i].poolCap*sizeof(kl_dpq_node_t *);

    /* blockQ */
    dpqSiz  += mpool.bpool[i].blockQ.ssz;
    dpqCap  += mpool.bpool[i].blockQ.scap;
    ovrhd   += mpool.bpool[i].blockQ.ssz*
                mpool.bpool[i].blockQ.cap*sizeof(kl_dpq_node_t);
    ovrhd   += mpool.bpool[i].blockQ.scap*sizeof(kl_dpq_node_t *);
    ovrhd   += KL_DPQ_SIZE*sizeof(kl_dpq_bucket_t);
  }

  klmi.pgsz     = KL_PAGESIZE; klmi.init     = mpool_is_init;
  klmi.splaysz  = splaySiz;
  klmi.splaycap = splayCap;
  klmi.poolact  = poolAct;
  klmi.poolsz   = poolSiz;
  klmi.poolcap  = poolCap;
  klmi.dpqsz    = dpqSiz;
  klmi.dpqcap   = dpqCap;
  klmi.ovrhd    = ovrhd;
  klmi.vmem     = KL_MXVMEM;
  klmi.rmem     = KL_MXREQ;
  klmi.over     = KL_OVER;

  return klmi;
}

void
klstats()
{
  unsigned long i, poolSiz=0, poolAct=0, poolCap=0, ovrhd=0;
  unsigned long dpqSiz=0, dpqCap=0;
  unsigned long splaySiz=0, splayCap=0;

  /* mpool */
  ovrhd    += sizeof(mpool_t);

  /* blockMap */
#ifdef KL_WITH_HMAP
#else
  splaySiz += mpool.blockMap.ssz;
  splayCap += mpool.blockMap.scap;
  ovrhd    += mpool.blockMap.ssz*mpool.blockMap.cap*sizeof(kl_splay_t);
  ovrhd    += mpool.blockMap.scap*sizeof(kl_splay_t *);
#endif

  for(i=0; i<MPOOL_INIT+1; ++i) {
    /* block */
    poolAct += mpool.bpool[i].poolAct;
    poolSiz += mpool.bpool[i].poolSiz;
    poolCap += mpool.bpool[i].poolCap;
    ovrhd   += mpool.bpool[i].poolCap*sizeof(block_t);

    /* blockMap */
    ovrhd   += mpool.bpool[i].poolCap*sizeof(kl_dpq_node_t *);

    /* blockQ */
    dpqSiz  += mpool.bpool[i].blockQ.ssz;
    dpqCap  += mpool.bpool[i].blockQ.scap;
    ovrhd   += mpool.bpool[i].blockQ.ssz*
                mpool.bpool[i].blockQ.cap*sizeof(kl_dpq_node_t);
    ovrhd   += mpool.bpool[i].blockQ.scap*sizeof(kl_dpq_node_t *);
    ovrhd   += KL_DPQ_SIZE*sizeof(kl_dpq_bucket_t);
  }

  fprintf(stderr, "\nklmalloc info -------------------------\n");
  fprintf(stderr, "  page size (B):  %21lu\n", KL_PAGESIZE);
  fprintf(stderr, "  mpool_is_init:  %21d\n", mpool_is_init);
  fprintf(stderr, "  mpool.blockMap.ssz:%18lu\n", splaySiz);
  fprintf(stderr, "  mpool.blockMap.scap:%17lu\n", splayCap);
  fprintf(stderr, "  bpool.poolAct:  %21lu\n", poolAct);
  fprintf(stderr, "  bpool.poolSiz:  %21lu\n", poolSiz);
  fprintf(stderr, "  bpool.poolCap:  %21lu\n", poolCap);
  fprintf(stderr, "  bpool.blockQ.ssz:%20lu\n", dpqSiz);
  fprintf(stderr, "  bpool.blockQ.scap:%19lu\n", dpqCap);
  fprintf(stderr, "  mpool.overhead (kB):%17.f\n", ovrhd/1000.0);
  fprintf(stderr, "  mpool.vmem (kB):%21.f\n", KL_MXVMEM/1000.0);
  fprintf(stderr, "\n");
  fprintf(stderr, "  mem reqst'd (kB): %19.f\n", KL_MXREQ/1000.0);
  fprintf(stderr, "  mem overage (kB): %19.f\n", KL_OVER/1000.0);
  fprintf(stderr, "\n");
}

#if defined(KL_MALLOC)
void *
malloc(
  size_t sz
)
{
  return klmalloc(sz);
}

void *
calloc(
  size_t num,
  size_t sz
)
{
  return klcalloc(num, sz);
}

void *
realloc(
  void * ptr,
  size_t sz
)
{
  return klrealloc(ptr, sz);
}

void
free(
  void * ptr
)
{
  klfree(ptr);
}
#endif
