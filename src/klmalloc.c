/*
                An implementation of dynamic memory allocator
                     J. Iverson <jiverson@cs.umn.edu>
                       Thu Mar 12 14:17:25 CDT 2015

  ...
*/

/*****************************************************************************
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
zero.

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
*****************************************************************************/


#ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200112L
#elif _POSIX_C_SOURCE < 200112L
# undef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200112L
#endif
#include <stdlib.h> /* posix_memalign, size_t */
#undef _POSIX_C_SOURCE

#include <assert.h>   /* assert */
#include <limits.h>   /* CHAR_BIT */
#include <stdint.h>   /* uint*_t */
#include <string.h>   /* memset */

/* This alignment should be a power of 2. */
#ifdef MEMORY_ALLOCATION_ALIGNMENT
# define KL_MEMORY_ALLOCATION_ALIGNMENT MEMORY_ALLOCATION_ALIGNMENT
#else
# define KL_MEMORY_ALLOCATION_ALIGNMENT 16
#endif

#ifndef KL_EXPORT
# define KL_EXPORT extern
#endif


#define KL_DEBUG 0
#if defined(KL_DEBUG) && KL_DEBUG > 0
# include <stdio.h>
# define KL_PRINT printf
#else
# define KL_NOOP(...)
# define KL_PRINT KL_NOOP
#endif
#include <stdio.h>


/****************************************************************************/
/* Relevant types */
/****************************************************************************/
typedef uintptr_t uptr;


/****************************************************************************/
/* Base 2 integer logarithm */
/****************************************************************************/
#if !defined(__INTEL_COMPILER) && !defined(__GNUC__)
  static int kl_builtin_clzl(size_t v) {
    /* result will be nonsense if v is 0 */
    int i;
    for (i=sizeof(size_t)*CHAR_BIT-1; i>=0; --i) {
      if (v & ((size_t)1 << i))
        break;
    }
    return sizeof(size_t)*CHAR_BIT-i-1;
  }
  #define kl_clz(V) kl_builtin_clzl(V)
#else
  #define kl_clz(V) __builtin_clzl(V)
#endif

#define KLLOG2(V) (sizeof(size_t)*CHAR_BIT-1-kl_clz(V))


/****************************************************************************/
/* Lookup tables to convert between size and bin number */
/****************************************************************************/
#define KLNUMBIN   1576
#define KLSMALLBIN 1532
#define KLISSMALLBIN(B) (B < KLSMALLBIN)

#define KLSIZE2BIN(S)                                                       \
  (                                                                         \
    (1==(S))                                                                \
      ? (size_t)0                                                           \
      : (KLLOG2((S)-1)<20)                                                  \
        ? log2off[KLLOG2((S)-1)]+(S)/log2size[KLLOG2((S)-1)]                \
        : log2off[KLLOG2((S)-1)]                                            \
  )

static size_t log2size[64]=
{
  8, 8, 8, 8, 8, 8, 16, 16, 32, 32, 64, 64, 128, 128, 256, 256, 512, 512,
  1024, 1024, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static size_t log2off[64]=
{
  0, 0, 0, 0, 0, 0, 8, 8, 20, 20, 44, 44, 92, 92, 188, 188, 380, 380, 764,
  764, 1532, 1533, 1534, 1535, 1536, 1537, 1538, 1539, 1540, 1541, 1542, 1543,
  1544, 1545, 1546, 1547, 1548, 1549, 1550, 1551, 1552, 1553, 1554, 1555,
  1556, 1557, 1558, 1559, 1560, 1561, 1562, 1563, 1564, 1565, 1566, 1567,
  1568, 1569, 1570, 1571, 1572, 1573, 1574, 1575
};


/****************************************************************************/
/* Align an unsigned integer value to a power supplied power of 2 */
/****************************************************************************/
#define KL_ALIGN(LEN, ALIGN) \
  (assert(0 == ((ALIGN)&((ALIGN)-1))), (((LEN)+((ALIGN)-1))&(~((ALIGN-1)))))


/****************************************************************************/
/* System memory allocation related macros */
/****************************************************************************/
#define KL_SYS_ALLOC_FAIL               NULL
#define KL_CALL_SYS_ALLOC(P,S)          (P)=malloc(S)
#define KL_CALL_SYS_ALLOC_ALIGN(P,A,S)  posix_memalign(&(P),A,S),memset(P,0,S)
#define KL_CALL_SYS_FREE(P,S)           free(P)


/****************************************************************************/
/* Relevant macros for a memory allocation block */
/****************************************************************************/
#define KL_BLOCK_SIZE     65536
#define KL_BLOCK_META \
  KL_ALIGN(sizeof(size_t)+sizeof(size_t), KL_MEMORY_ALLOCATION_ALIGNMENT)
#define KL_BLOCK_ALIGN    KL_BLOCK_SIZE
#define KL_BLOCK_SIZ(BLK) (*(size_t*)(BLK))
#define KL_BLOCK_CNT(BLK) (*(size_t*)((uptr)(BLK)+sizeof(size_t)))


/****************************************************************************/
/* Compute the size of a memory chunk given an initial size, adjusts for the
 * extra memory required for meta info and alignment */
/****************************************************************************/
#define KL_CHUNK_SIZE(S) \
  (KL_ALIGN(sizeof(void*)+sizeof(size_t)+(S), KL_MEMORY_ALLOCATION_ALIGNMENT))


/****************************************************************************/
/* Access macros for a memory chunk */
/****************************************************************************/
#define KL_CHUNK_SIZ(MEM)       (*(size_t*)((uptr)(MEM)+sizeof(void*)))
#define KL_CHUNK_PTR(MEM)       (void*)((uptr)(MEM)+sizeof(void*)+sizeof(size_t))
#define KL_CHUNK_MEM(PTR)       (void*)((uptr)(PTR)-sizeof(void*)-sizeof(size_t))
#define KL_CHUNK_BLK(MEM)       (void*)((uptr)(MEM)&(~(KL_BLOCK_SIZE-1)))
#define KL_CHUNK_TWO(MEM, SIZE) (void*)((uptr)(MEM)+KL_CHUNK_SIZE(SIZE))


/****************************************************************************/
/****************************************************************************/
/* Free chunk data structure API */
/****************************************************************************/
/****************************************************************************/

/****************************************************************************/
/* Free chunk data structure node */
/****************************************************************************/
typedef struct kl_bin_node
{
  struct kl_bin_node * n; /* next node */
} kl_bin_node_t;


/****************************************************************************/
/* Free chunk data structure bin */
/****************************************************************************/
typedef struct kl_bin_bin
{
  struct kl_bin_node * hd; /* head node */
} kl_bin_bin_t;


/****************************************************************************/
/* Free chunk data structure */
/****************************************************************************/
typedef struct kl_bin
{
  int init;
  struct kl_bin_bin bin[KLNUMBIN];
} kl_bin_t;


/****************************************************************************/
/* Initialize free chunk data structure */
/****************************************************************************/
static int
kl_bin_init(kl_bin_t * const bin)
{
  int i;

  for (i=0; i<KLNUMBIN; ++i)
    bin->bin[i].hd = NULL;

  bin->init = 1;

  return 0;
}


/****************************************************************************/
/* Add a node to a free chunk data structure */
/****************************************************************************/
static int
kl_bin_ad(kl_bin_t * const bin, kl_bin_node_t * const node)
{
  size_t bidx = KLSIZE2BIN(KL_CHUNK_SIZ(node));
  kl_bin_node_t * p, * n;

  /* Treat fixed size bins and large bins differently */
  if (KLISSMALLBIN(bidx)) {
    KL_PRINT("klinfo: adding available fixed size memory chunk\n");
    KL_PRINT("klinfo:   chunk address: %p\n", (void*)node);
    KL_PRINT("klinfo:   chunk size:    %zu\n", KL_CHUNK_SIZ(node));
    KL_PRINT("klinfo:   bin index:     %zu\n", bidx);

    /* prepend n to front of bin[bidx] linked-list */
    node->n = bin->bin[bidx].hd;
    bin->bin[bidx].hd = node;
  }
  else {
    KL_PRINT("klinfo: adding available variable size memory chunk\n");
    KL_PRINT("klinfo:   chunk address: %p\n", (void*)node);
    KL_PRINT("klinfo:   chunk size:    %zu\n",
      KL_CHUNK_SIZ(KL_CHUNK_MEM(node)));
    KL_PRINT("klinfo:   bin index:    %zu\n", bidx);

    /* this will keep large buckets sorted */
    n = bin->bin[bidx].hd;
    p = NULL;

    while (NULL != n && KL_CHUNK_SIZ(n) < KL_CHUNK_SIZ(node)) {
      p = n;
      n = n->n;
    }

    if (NULL == p)
      bin->bin[bidx].hd = node;
    else
      p->n = node;
    node->n = n;
  }

  KL_PRINT("klinfo:\n");

  return 0;
}


/****************************************************************************/
/* Find the bin with the smallest size >= size parameter */
/****************************************************************************/
static void *
kl_bin_find(kl_bin_t * const bin, size_t const size)
{
  size_t bidx = KLSIZE2BIN(size);
  kl_bin_node_t * p, * n;

  if (KLISSMALLBIN(bidx)) {
    KL_PRINT("klinfo: searching for available fixed size memory chunk\n");
    KL_PRINT("klinfo:   request size:    %zu\n", size);
    KL_PRINT("klinfo:   bin index:       %zu\n", bidx);
    KL_PRINT("klinfo:   head of bin:     %p\n", (void*)bin->bin[bidx].hd);

    /* Find first bin with a node. */
    n = bin->bin[bidx].hd;
    while (NULL == n && bidx < KLSMALLBIN)
      n = bin->bin[++bidx].hd;

    KL_PRINT("klinfo:   final bin index: %zu\n", bidx);
    KL_PRINT("klinfo:\n");

    /* Remove head of bin[bidx]. */
    if (NULL != n) {
      assert(NULL != bin->bin[bidx].hd);
      bin->bin[bidx].hd = n->n;
      n->n = NULL;
    }
  }
  else {
    KL_PRINT("klinfo: searching for available variable size memory chunk\n");
    KL_PRINT("klinfo:   request size: %zu\n", size);

    /* Find first bin with a node. */
    n = bin->bin[bidx].hd;
    while (NULL == n && bidx < KLNUMBIN)
      n = bin->bin[++bidx].hd;

    /* Find first node in bin[bidx] with size >= size parameter. */
    p = NULL;
    while (NULL != n && KL_CHUNK_SIZ(n) < size) {
      p = n;
      n = n->n;
    }

    /* Remove n from bin[bidx]. */
    if (NULL != n) {
      if (NULL == p)
        bin->bin[bidx].hd = n->n;
      else
        p->n = n->n;
      n->n = NULL;
    }
  }

  if (NULL != n) {
    KL_PRINT("klinfo:   available chunk found\n");
    KL_PRINT("klinfo:     chunk address: %p\n", (void*)n);
  }
  else {
    KL_PRINT("klinfo:   no available chunk found\n");
  }

  KL_PRINT("klinfo:\n");

  return n;
}


/****************************************************************************/
/****************************************************************************/
/* KL API */
/****************************************************************************/
/****************************************************************************/

/****************************************************************************/
/* Free chunk data structure */
/****************************************************************************/
static kl_bin_t bin={.init=0};


/****************************************************************************/
/* Initialize static variables and data structures */
/****************************************************************************/
#define KL_INIT_CHECK                                                       \
do {                                                                        \
  if (0 == bin.init)                                                        \
    kl_bin_init(&bin);                                                      \
} while (0)


/****************************************************************************/
/* Allocate size bytes of memory */
/****************************************************************************/
KL_EXPORT void *
klmalloc(size_t const size)
{
  int ret;
  size_t msize;
  void * mem;

  KL_INIT_CHECK;

  KL_PRINT("klinfo: klmalloc beg\n");

  /* Try to get a previously allocated block of memory. */
  /* kl_bin_find() will remove chunk from free chunk data structure. */
  if (NULL == (mem=kl_bin_find(&bin, size))) {
    /* If no previously allocated block of memory can support this allocation,
     * then allocate a new block.  If requested size is less than
     * KL_BLOCK_MAX-KL_BLOCK_META, then allocate a new block.  Otherwise,
     * directly allocate the required amount of memory. */
    if (KL_CHUNK_SIZE(size) <= KL_BLOCK_SIZE-KL_BLOCK_META) {
      KL_PRINT("klinfo: allocating a new fixed size block of memory\n");
      msize = KL_BLOCK_SIZE;
    }
    else {
      KL_PRINT("klinfo: allocating a new variable size block of memory\n");
      msize = KL_BLOCK_META+KL_CHUNK_SIZE(size);
    }

    /* Get system memory */
    ret = KL_CALL_SYS_ALLOC_ALIGN(mem, KL_BLOCK_ALIGN, msize);
    if (-1 == ret)
      return NULL;
    if (KL_SYS_ALLOC_FAIL == mem)
      return NULL;

    printf("klinfo: [%8p] allocation\n", mem);
    KL_PRINT("klinfo:   block start: %p\n", (void*)mem);
    KL_PRINT("klinfo:   size start:  %p\n", (void*)&(KL_BLOCK_SIZ(mem)));
    KL_PRINT("klinfo:   count start: %p\n", (void*)&(KL_BLOCK_CNT(mem)));
    KL_PRINT("klinfo:   block size:  %zu\n", msize);
    KL_PRINT("klinfo:\n");

    /* Set block size */
    KL_BLOCK_SIZ(mem) = msize-KL_BLOCK_META;

    /* Set mem to be the chunk to be returned */
    mem = (void*)((uptr)mem+KL_BLOCK_META);

    /* Set chunk size */
    KL_CHUNK_SIZ(mem) = KL_BLOCK_SIZE-KL_BLOCK_META;
  }

  /* TODO: need to prevent splitting of large blocks for the time being, since
   * doing so arbitrarily will render the KL_CHUNK_BLK macro invalid, since a
   * many chunks may be created from one large block, some of which my be
   * beyond the KL_BLOCK_SIZE limit that associates a chunk with a block. */
  if (KL_BLOCK_SIZ(mem) <= KL_BLOCK_SIZE-KL_BLOCK_META) {
    /* Conceptually break mem into two chunks:
     *   mem[0..KL_CHUNK_SIZE(size)-1], mem[KL_CHUNK_SIZE(size)..msize]
     * Add the second chunk to free chunk data structure, when applicable. */
    if (KL_CHUNK_SIZE(size) < KL_CHUNK_SIZ(mem)) {
      KL_PRINT("klinfo:   splitting block into 2 chunk(s)\n");
      KL_PRINT("klinfo:     chunk[0]:\n");
      KL_PRINT("klinfo:       system address:  %p\n", mem);
      KL_PRINT("klinfo:       system size:     %zu\n", KL_CHUNK_SIZE(size));
      KL_PRINT("klinfo:       program address: %p\n", KL_CHUNK_PTR(mem));
      KL_PRINT("klinfo:       program size:    %zu\n", size);
      KL_PRINT("klinfo:     chunk[1]:\n");
      KL_PRINT("klinfo:       system address:  %p\n", KL_CHUNK_TWO(mem, size));
      KL_PRINT("klinfo:       system size:     %zu\n",
        KL_CHUNK_SIZ(mem)-KL_CHUNK_SIZE(size));
      KL_PRINT("klinfo:\n");

      /* set chunk[1] size and add it to free chunk data structure */
      KL_CHUNK_SIZ(KL_CHUNK_TWO(mem, size)) = KL_CHUNK_SIZ(mem)-KL_CHUNK_SIZE(size);
      if (0 != kl_bin_ad(&bin, KL_CHUNK_TWO(mem, size)))
        return NULL;

      /* update new chunk[0] size */
      KL_CHUNK_SIZ(mem) = KL_CHUNK_SIZE(size);
    }
    else {
      KL_PRINT("klinfo:   splitting block into 1 chunk(s)\n");
      KL_PRINT("klinfo:     chunk[0]:\n");
      KL_PRINT("klinfo:       system address:  %p\n", mem);
      KL_PRINT("klinfo:       system size:     %zu\n", KL_CHUNK_SIZ(mem));
      KL_PRINT("klinfo:       program address: %p\n", KL_CHUNK_PTR(mem));
      KL_PRINT("klinfo:       program size:    %zu\n", size);
      KL_PRINT("klinfo:\n");
    }
  }

  /* Increment count for containing block. */
  KL_BLOCK_CNT(KL_CHUNK_BLK(mem))++;

  printf("klinfo: [%8p] incrementing block count: %zu\n", KL_CHUNK_BLK(mem),
    KL_BLOCK_CNT(KL_CHUNK_BLK(mem)));
  KL_PRINT("klinfo: incrementing block count: %zu\n",
    KL_BLOCK_CNT(KL_CHUNK_BLK(mem)));
  KL_PRINT("klinfo: klmalloc end\n");
  KL_PRINT("klinfo:\n");

  return KL_CHUNK_PTR(mem);
}


/****************************************************************************/
/* Allocate num*size bytes of zeroed memory */
/****************************************************************************/
KL_EXPORT void *
klcalloc(size_t const num, size_t const size)
{
  KL_INIT_CHECK;

  void * ptr=klmalloc(num*size);
  if (NULL != ptr)
    memset(ptr, 0, num*size);
  return ptr;
}


/****************************************************************************/
/* Release size bytes of memory */
/****************************************************************************/
KL_EXPORT void
klfree(void * const ptr)
{
  /* TODO: one issue with current implementation is the following.  When a
   * large block gets split into smaller blocks, the ability to use the
   * KL_CHUNK_BLK() macro goes away, since the large block could get split up
   * such that a chunk beyond the KL_BLOCK_SIZE limit is allocated, thus when
   * KL_CHUNK_BLK() is called it will return a memory location of something
   * which is not a block.  To address this, large blocks are not allowed to
   * be split. */
  /* TODO: another issue with current implementation is the following.  When a
   * block count gets decremented to zero, its associated memory is freed.
   * However, no where does the system account for any chunks that remain in
   * the free chunk data structure.  This is espicially problematic becuase
   * those chunks still in the free chunk data structure cannot be removed in
   * place, due to the singly-linked nature of the data structure. */

  KL_INIT_CHECK;

  KL_PRINT("klinfo: freeing from a block with %d chunk(s)\n",
    KL_BLOCK_CNT(KL_CHUNK_BLK(KL_CHUNK_MEM(ptr))));
  KL_PRINT("klinfo:   block address:  %p\n",
    KL_CHUNK_BLK(KL_CHUNK_MEM(ptr)));
  KL_PRINT("klinfo:   chunk address:  %p\n", KL_CHUNK_MEM(ptr));

  /* Sanity check to make sure the containing block is somewhat valid. */
  assert(0 != KL_BLOCK_CNT(KL_CHUNK_BLK(KL_CHUNK_MEM(ptr))));

  /* TODO: Implicitly, the following rule prevents large allocations from
   * going into the free chunk data structure.  Thus, it also prevents the
   * limitation described above.  However, in many cases, it would be nice to
   * keep large allocations around for quicker allocation time. */

  printf("klinfo: [%8p] decrementing block count: %zu\n",
    KL_CHUNK_BLK(KL_CHUNK_MEM(ptr)),
    KL_BLOCK_CNT(KL_CHUNK_BLK(KL_CHUNK_MEM(ptr))));
  /* Decrement count for containing block and release entire block if it is
   * empty. */
  if (0 == --KL_BLOCK_CNT(KL_CHUNK_BLK(KL_CHUNK_MEM(ptr)))) {
    KL_PRINT("klinfo:   releasing block back to system\n");

    KL_CALL_SYS_FREE(KL_CHUNK_BLK(KL_CHUNK_MEM(ptr)),
      KL_BLOCK_SIZ(KL_CHUNK_BLK(KL_CHUNK_MEM(ptr))+KL_BLOCK_META));

    KL_PRINT("klinfo:\n");
  }
  /* Otherwise, add the chunk back into free chunk data structure. */
  else {
    KL_PRINT("klinfo:   adding chunk back into free chunk data structure\n");
    KL_PRINT("klinfo:\n");

    /* TODO: check to see if chunk can be coalesced.  Coalescing allocations
     * could cause large allocations to exist in the free chunk data structure
     * and thus, if coalescing is used, the system must then address the
     * issure of splitting large allocations and its affect on the
     * KL_CHUNK_BLK macro. */
    if (0 != kl_bin_ad(&bin, KL_CHUNK_MEM(ptr)))
      return;
  }
}
