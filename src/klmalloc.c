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

/****************************************************************************/
/****************************************************************************/
/* Free chunk data structure */
/****************************************************************************/
/****************************************************************************/

/****************************************************************************/
/* Base 2 integer logarithm */
/****************************************************************************/
#if !defined(__INTEL_COMPILER) && !defined(__GNUC__)
  static int kl_builtin_popcountl(size_t v) {
    int c = 0;
    for (; v; c++) {
      v &= v-1;
    }
    return c;
  }
  static int kl_builtin_clzl(size_t v) {
    /* result will be nonsense if v is 0 */
    int i;
    for (i=sizeof(size_t)*CHAR_BIT-1; i>=0; --i) {
      if (v & (1ul << i)) {
        break;
      }
    }
    return sizeof(size_t)*CHAR_BIT-i-1;
  }
  static int kl_builtin_ctzl(size_t v) {
    /* result will be nonsense if v is 0 */
    int i;
    for (i=0; i<sizeof(size_t)*CHAR_BIT; ++i) {
      if (v & (1ul << i)) {
        return i;
      }
    }
    return i;
  }
  #define kl_popcount(V)  kl_builtin_popcountl(V)
  #define kl_clz(V)       kl_builtin_clzl(V)
  #define kl_ctz(V)       kl_builtin_ctzl(V)
#else
  #define kl_popcount(V)  __builtin_popcountl(V)
  #define kl_clz(V)       __builtin_clzl(V)
  #define kl_ctz(V)       __builtin_ctzl(V)
#endif

#define KLLOG2(V) (sizeof(size_t)*CHAR_BIT-1-kl_clz(v))


/****************************************************************************/
/* Lookup tables to convert between size and bin number */
/****************************************************************************/
#define KLMAXBIN  379   /* zero indexed, so there are 379 bins */
#define KLMAXSIZE 65536 /* ... */

#define KLSIZE2BIN(S2B,S) ((S) <= KLMAXSIZE ? (S2B)[(S)] : -1)


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
  int size2bin[KLMAXSIZE+1];
  struct kl_bin_bin bin[KLMAXBIN+1];
} kl_bin_t;


/****************************************************************************/
/* Initialize free chunk data structure */
/****************************************************************************/
static int
kl_bin_init(kl_bin_t * const bin)
{
  int i;

  bin->size2bin[0] = -1;
  for (i=1; i<=KLMAXSIZE; ++i) {
    if (i <= 64)
      bin->size2bin[i] = (i-1)/8;
    else if (i <=   256)
      bin->size2bin[i] = 8+(i-65)/16;
    else if (i <=  1024)
      bin->size2bin[i] = 20+(i-257)/32;
    else if (i <=  4096)
      bin->size2bin[i] = 44+(i-1025)/64;
    else if (i <= 16384)
      bin->size2bin[i] = 92+(i-4097)/128;
    else if (i <= 65536)
      bin->size2bin[i] = 188+(i-16385)/256;
  }

  for (i=0; i<=KLMAXBIN; ++i)
    bin->bin[i].hd = NULL;

  return 0;
}


/****************************************************************************/
/* Add a node to a free chunk data structure */
/****************************************************************************/
static int
kl_bin_ad(kl_bin_t * const bin, kl_bin_node_t * const n, size_t const size)
{
  int bidx = KLSIZE2BIN(bin->size2bin, size);

  /* prepend n to front of bin[bidx] linked-list */
  n->n = bin->bin[bidx].hd;
  bin->bin[bidx].hd = n;

  return 0;
}


/****************************************************************************/
/* Find the bin with the smallest size >= size parameter */
/****************************************************************************/
static void *
kl_bin_find(kl_bin_t * const bin, size_t const size)
{
  int bidx;
  kl_bin_node_t * hd;

  bidx = KLSIZE2BIN(bin->size2bin, size);

  /* Find first bin with a node and size >= size parameter. */
  hd = bin->bin[bidx].hd;
  while (NULL == hd && bidx < KLMAXBIN)
    hd = bin->bin[++bidx].hd;

  /* Remove head of bin[bidx]. */
  if (NULL != hd)
    bin->bin[bidx].hd = hd->n;

  return hd;
}


/****************************************************************************/
/****************************************************************************/
/* KL API */
/****************************************************************************/
/****************************************************************************/

/****************************************************************************/
/* Relevant types */
/****************************************************************************/
typedef uintptr_t uptr;


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
/* Align an unsigned integer value to a power supplied power of 2 */
/****************************************************************************/
#define KL_ALIGN(LEN, ALIGN) \
  (assert(0 == ((ALIGN)&((ALIGN)-1))), (((LEN)+((ALIGN)-1))&(~((ALIGN-1)))))


/****************************************************************************/
/* System memory allocation related macros */
/****************************************************************************/
#define KL_SYS_ALLOC_FAIL               NULL
#define KL_CALL_SYS_ALLOC(P,S)          (P)=malloc(S)
#define KL_CALL_SYS_ALLOC_ALIGN(P,A,S)  posix_memalign(&(P),A,S)
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
#define KL_CHUNK_SIZ(MEM) (*(size_t*)((uptr)(MEM)+sizeof(void*)))
#define KL_CHUNK_PTR(MEM) (void*)((uptr)(MEM)+sizeof(void*)+sizeof(size_t))
#define KL_CHUNK_MEM(PTR) (void*)((uptr)(PTR)-sizeof(void*)-sizeof(size_t))
#define KL_CHUNK_BLK(MEM) (void*)((uptr)(MEM)&(~(KL_BLOCK_SIZE-1)))


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

  /* Try to get a previously allocated block of memory. */
  /* kl_bin_find() will remove chunk from free chunk data structure. */
  if (NULL == (mem=kl_bin_find(&bin, size))) {
    /* If no previously allocated block of memory can support this allocation,
     * then allocate a new block.  If requested size is less than
     * KL_BLOCK_MAX-KL_BLOCK_META, then allocate a new block.  Otherwise,
     * directly allocate the required amount of memory. */
    if (KL_CHUNK_SIZE(size) <= KL_BLOCK_SIZE-KL_BLOCK_META)
      msize = KL_BLOCK_SIZE;
    else
      msize = KL_BLOCK_META+KL_CHUNK_SIZE(size);

    /* Get system memory */
    ret = KL_CALL_SYS_ALLOC_ALIGN(mem, KL_BLOCK_ALIGN, msize);
    if (-1 == ret)
      return NULL;
    if (KL_SYS_ALLOC_FAIL == mem)
      return NULL;

    /* Set block size */
    KL_BLOCK_SIZ(mem) = msize;

    /* Set mem to be the chunk to be returned */
    mem = (void*)((uptr)mem+KL_BLOCK_META);

    /* Set chunk size */
    KL_CHUNK_SIZ(mem) = KL_CHUNK_SIZE(size);
  }

  /* Conceptually break mem into two chunks:
   *   mem[0..KL_CHUNK_SIZE(size)-1], mem[KL_CHUNK_SIZE(size)..msize]
   * Add the second chunk to free chunk data structure, when applicable. */
  if (KL_CHUNK_SIZE(size) < KL_CHUNK_SIZE(KL_CHUNK_SIZ(mem))) {
    if (0 != kl_bin_ad(&bin, (void*)((uptr)mem+KL_CHUNK_SIZE(size)),
        KL_CHUNK_SIZE(KL_CHUNK_SIZ(mem))-KL_CHUNK_SIZE(size)))
    {
      return NULL;
    }
    KL_CHUNK_SIZ((void*)((uptr)mem+KL_CHUNK_SIZE(size))) =
      KL_CHUNK_SIZE(KL_CHUNK_SIZ(mem))-KL_CHUNK_SIZE(size);
  }

  /* Increment count for containing block. */
  KL_BLOCK_CNT(KL_CHUNK_BLK(mem))++;

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
   * which is not a block. */
  KL_INIT_CHECK;

  /* Sanity check to make sure the containing block is somewhat valid. */
  assert(0 != KL_BLOCK_CNT(KL_CHUNK_BLK(KL_CHUNK_MEM(ptr))));

  /* Decrement count for containing block and release entire block if it is
   * empty. */
  if (0 == --KL_BLOCK_CNT(KL_CHUNK_BLK(KL_CHUNK_MEM(ptr)))) {
    KL_CALL_SYS_FREE(KL_CHUNK_BLK(KL_CHUNK_MEM(ptr)),
      KL_BLOCK_SIZ(KL_CHUNK_BLK(KL_CHUNK_MEM(ptr))));
  }
  /* Otherwise, add the chunk back into free chunk data structure. */
  else {
    /* TODO: check to see if chunk can be coalesced */
    if (0 != kl_bin_ad(&bin, KL_CHUNK_MEM(ptr), KL_CHUNK_SIZ(KL_CHUNK_MEM(ptr))))
      return;
  }
}
