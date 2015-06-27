/*
Copyright (c) 2015, Jeremy Iverson
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

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

/* TODO:
 *  1. Allow coalescing of adjacent blocks.
 */

#include "klconfig.h"

#include <assert.h>   /* assert */
#include <limits.h>   /* CHAR_BIT */
#include <malloc.h>   /* struct mallinfo */
#include <stdint.h>   /* uintptr_t */
#include <string.h>   /* memset */

#include "klmalloc.h" /* klmalloc library */


/****************************************************************************/
/*! Required function prototypes. */
/****************************************************************************/
void * libc_malloc(size_t const size);
void * libc_calloc(size_t const num, size_t const size);
void * libc_realloc(void * const ptr, size_t const size);
void   libc_free(void * const ptr);


#ifndef KL_EXPORT
# define KL_EXPORT extern
#endif


/* This alignment should be a power of 2 >= sizeof(size_t). */
#ifndef MEMORY_ALLOCATION_ALIGNMENT
# define MEMORY_ALLOCATION_ALIGNMENT 8
#endif


/****************************************************************************/
/* Memory data structures. */
/****************************************************************************/
#define BLOCK_DEFAULT_SIZE 262144

#define BLOCK_HEADER_ALIGN                                                  \
(                                                                           \
  MEMORY_ALLOCATION_ALIGNMENT == sizeof(uintptr_t)                          \
    ? MEMORY_ALLOCATION_ALIGNMENT                                           \
    : MEMORY_ALLOCATION_ALIGNMENT-sizeof(uintptr_t)                         \
)

#define FIXED_MAX_SIZE                                                      \
  (BLOCK_DEFAULT_SIZE-BLOCK_HEADER_ALIGN-sizeof(kl_brick_t*)-               \
  2*sizeof(struct kl_fix_block*))

typedef struct kl_alloc
{
  uintptr_t info;
  char * raw;
} kl_alloc_t;

typedef struct kl_brick
{
  uintptr_t info;
  union {
    struct kl_brick_node
    {
      struct kl_brick * next;
    } node;
    char * raw;
  } iface;
} kl_brick_t;

typedef struct kl_chunk
{
  uintptr_t info;
  union {
    struct kl_chunk_node
    {
      struct kl_chunk * prev;
      struct kl_chunk * next;
      uintptr_t footer;
    } node;
    char * raw;
  } iface;
} kl_chunk_t;

typedef struct kl_block
{
  char _pad[BLOCK_HEADER_ALIGN];
  char raw[];
} kl_block_t;

typedef struct kl_fix_block
{
  char _pad[BLOCK_HEADER_ALIGN];
  char raw[FIXED_MAX_SIZE];
  kl_brick_t * head;
  struct kl_fix_block * prev;
  struct kl_fix_block * next;
} kl_fix_block_t;

typedef struct kl_var_block
{
  char _pad[BLOCK_HEADER_ALIGN];
  char raw[];
} kl_var_block_t;

#define ALLOC_HDR(A)  \
  ((A)->info)
#define ALLOC_PTR(A)  \
  ((void*)&(A)->raw)

#define BRICK_HDR(B)  \
  ((B)->info)
#define BRICK_PTR(B)  \
  ((void*)&(B)->iface.raw)
#define BRICK_PREV(B) \
  ((B)->iface.node.prev)
#define BRICK_NEXT(B) \
  ((B)->iface.node.next)

#define CHUNK_HDR(C)  \
  ((C)->info)
#define CHUNK_FTR(C)  \
  (*(uintptr_t*)((uintptr_t)(C)+CHUNK_HDR(C)-sizeof(uintptr_t)))
#define CHUNK_PTR(C)  \
  ((void*)&(C)->iface.raw)
#define CHUNK_PREV(C) \
  ((C)->iface.node.prev)
#define CHUNK_NEXT(C) \
  ((C)->iface.node.next)

#define BLOCK_HDR(B)    \
  (*(uintptr_t*)((uintptr_t)&(B)->raw-sizeof(uintptr_t)))
#define BLOCK_FTR(B,S)  \
  (*(uintptr_t*)((uintptr_t)(B)+(S)-sizeof(kl_brick_t*)-  \
  2*sizeof(struct kl_fix_block*)))
#define BLOCK_PTR(B)    \
  ((kl_alloc_t*)&(B)->raw)
#define BLOCK_HEAD(B)   \
  ((B)->head)
#define BLOCK_PREV(B)   \
  ((B)->prev)
#define BLOCK_NEXT(B)   \
  ((B)->next)


/****************************************************************************/
/* Compile time property checks. */
/****************************************************************************/
#define ct_assert_concat_(a, b) a##b
#define ct_assert_concat(a, b) ct_assert_concat_(a, b)
#define ct_assert(e) enum {ct_assert_concat(assert_line_,__LINE__)=1/(!!(e))}

/* Sanity check: void * and uintptr_t are the same size, assumed in many
 * places in the code. */
ct_assert(sizeof(void*) == sizeof(uintptr_t));
/* Sanity check: Alignment is >= 2. */
ct_assert(MEMORY_ALLOCATION_ALIGNMENT >= 2);
/* Sanity check: Alignment is >= sizeof(uintptr_t). */
ct_assert(MEMORY_ALLOCATION_ALIGNMENT >= sizeof(uintptr_t));
/* Sanity check: MEMORY_ALLOCATION_ALIGNMENT is a power of 2. */
ct_assert(0 == (MEMORY_ALLOCATION_ALIGNMENT&(MEMORY_ALLOCATION_ALIGNMENT-1)));
/* Sanity check: fixed block struct size is correct. */
ct_assert(262144 == sizeof(kl_fix_block_t));


/****************************************************************************/
/* Types of allocations */
/****************************************************************************/
enum {
  KL_CHUNK=0,
  KL_BRICK=1
};


/****************************************************************************/
/*
 *  Memory block:
 *
 *    | size_t | `memory chunks' | size_t |
 *    +--------+-----------------+--------+
 *
 *    Memory blocks must be allocated so that their starting address is
 *    aligned to MEMORY_ALLOCATION_ALIGNMENT, which is a power of 2.  For
 *    memory bricks, this is not necessarily required.  However, for memory
 *    chunks, this allows for the first and last memory chunks to be easily
 *    identifiable. In this case, these chunks are identified as the chunk
 *    which is preceded / succeeded by a size_t with the value
 *    KL_BLOCK_HEADER_SIZE, respectively.
 *
 *
 *  Memory brick:
 *
 *      ACTIVE  | void * | 'used memory' |
 *              +--------+---------------+
 *              `        ^
 *
 *    INACTIVE  | void * | void * | 'unused memory' |
 *              +--------+--------+-----------------+
 *              `        ^
 *
 *    The pointer address, indicated by ^ above, must be aligned to
 *    MEMORY_ALLOCATION_ALIGNMENT.  Thus, if MEMORY_ALLOCATION_ALIGNMENT
 *    > sizeof(size_t), the chunk address, indicated by ` above must be
 *    aligned to an address that is sizeof(size_t) less than a
 *    MEMORY_ALLOCATION_ALIGNMENT aligned address.  To accomplish this for
 *    consecutive chunks, the size of each chunk is computed so that the
 *    following chunk will start at an address that is sizeof(size_t) less
 *    than a MEMORY_ALLOCATION_ALIGNMENT aligned address.  Assuming that a
 *    given chunk is properly aligned, it is sufficient to find the first
 *    aligned size >= the requested size plus the per-chunk overhead.
 *
 *    For all memory bricks, the first `void *' shall point to the block
 *    header for the block to which the brick belongs.  This allows for
 *    constant time look up of brick size as well as constant time checks to
 *    see when a block is empty and should be released.  When a memory brick
 *    is inactive, the second `void *' shall point to the next available
 *    memory brick with the same size.
 *
 *
 *  Memory chunk:
 *
 *      ACTIVE  | size_t | 'used memory' | size_t |
 *              +--------+---------------+--------+
 *              `        ^
 *
 *    INACTIVE  | size_t | void * | void * | 'unused memory' | size_t |
 *              +--------+--------+--------+-----------------+--------+
 *              `        ^
 *
 *    Pointer addresses obey the same constraints as explained for memory
 *    bricks.
 *
 *    When a memory chunk is active, the final size_t shall contain the value
 *    0, and when inactive, shall contain the same value as the first size_t,
 *    which is the size of the entire memory chunk.  When a memory chunk is
 *    inactive, then the two `void *' shall be used for the pointers in the
 *    doubly-linked list in the free chunk data structure.
 *
 *  Abbriviations:
 *
 *    A = after chunk
 *    B = block
 *    C = chunk
 *    E = before chunk
 *    F = footer size
 *    H = header size
 *    M = unit multiplier
 *    P = pointer ([active=memory], [inactive=linked list])
 *    R = brick
 */
/****************************************************************************/
#define BLOCK_COUNT_MASK (((uintptr_t)1<<((sizeof(uintptr_t)-1)*CHAR_BIT))-1)
#define BLOCK_BIDX_MASK  (~(((uintptr_t)1<<((sizeof(uintptr_t)-1)*CHAR_BIT))-1))
#define BLOCK_BIDX_SHIFT ((sizeof(uintptr_t)-1)*CHAR_BIT)

#define ALLOC_MAX_SIZE   CHUNK_MAX_SIZE

#define BRICK_MAX_SIZE   (CHUNK_MIN_SIZE-1)

#define CHUNK_MIN_SIZE   (sizeof(kl_chunk_t))
#define CHUNK_MAX_SIZE   \
  ((SIZE_MAX&(~((size_t)MEMORY_ALLOCATION_ALIGNMENT)-1))-2*sizeof(uintptr_t))

static inline uintptr_t KL_ALIGN(uintptr_t const size)
{
  uintptr_t const align = MEMORY_ALLOCATION_ALIGNMENT;
  uintptr_t const size_aligned = (size+(align-1))&(~(align-1));

  /* Sanity check: alignment is power of 2. */
  assert(0 == (align&(align-1)));
  /* Sanity check: size_aligned is properly aligned. */
  assert(0 == (size_aligned&(align-1)));

  return size_aligned;
}

static inline int KL_ISALIGNED(void const * const ptr)
{
  return (uintptr_t)ptr == KL_ALIGN((uintptr_t)ptr);
}

static inline size_t KL_BLOCK_SIZE(size_t size)
{
  /* BLOCK_DEFAULT_SIZE-FIXED_MAX_SIZE is the per-block overhead. */
  return KL_ALIGN(BLOCK_DEFAULT_SIZE-FIXED_MAX_SIZE+size);
}

static inline size_t KL_BRICK_SIZE(size_t size)
{
  return KL_ALIGN(sizeof(uintptr_t)+size);
}

static inline size_t KL_CHUNK_SIZE(size_t size)
{
  return KL_ALIGN(2*sizeof(uintptr_t)+size);
}

static inline int KL_TYPEOF(void const * const addr)
{
  kl_alloc_t * alloc;

  alloc = (kl_alloc_t*)addr;

  assert(KL_ISALIGNED(&alloc->raw));

  return ALLOC_HDR(alloc)&KL_BRICK;
}

static inline int KL_G_BRICKBIN(size_t const size)
{
  return KL_BRICK_SIZE(size)/MEMORY_ALLOCATION_ALIGNMENT-1;
}

static inline size_t KL_G_COUNT(kl_fix_block_t const * const block)
{
  assert(KL_ISALIGNED((void*)block));
  return (size_t)(BLOCK_HDR(block)&BLOCK_COUNT_MASK);
}

static inline size_t KL_G_BIDX(kl_fix_block_t const * const block)
{
  assert(KL_ISALIGNED((void*)block));
  return (size_t)((BLOCK_HDR(block)&BLOCK_BIDX_MASK)>>BLOCK_BIDX_SHIFT);
}

static inline void * KL_G_BLOCK(void const * const addr)
{
  void * ptr=NULL;
  kl_alloc_t * alloc;
  kl_brick_t * brick;
  kl_chunk_t * chunk;

  alloc = (kl_alloc_t*)addr;

  assert(KL_ISALIGNED(ALLOC_PTR(alloc)));

  switch (KL_TYPEOF(alloc)) {
    case KL_BRICK:
      brick = (kl_brick_t*)alloc;
      ptr = (void*)(BRICK_HDR(brick)&(~((uintptr_t)KL_BRICK)));
      break;
    case KL_CHUNK:
      chunk = (kl_chunk_t*)alloc;
      ptr = (void*)((uintptr_t)chunk-BLOCK_HEADER_ALIGN);
      break;
  }

  assert(KL_ISALIGNED(ptr));

  return ptr;
}

static inline size_t KL_G_SIZE(void const * const addr)
{
  kl_alloc_t * alloc;
  kl_brick_t * brick;
  kl_chunk_t * chunk;
  kl_fix_block_t * block;

  alloc = (kl_alloc_t*)addr;

  assert(KL_ISALIGNED(ALLOC_PTR(alloc)));

  switch (KL_TYPEOF(alloc)) {
    case KL_BRICK:
      brick = (kl_brick_t*)alloc;
      block = (kl_fix_block_t*)KL_G_BLOCK((kl_alloc_t*)brick);
      return (KL_G_BIDX(block)+1)*MEMORY_ALLOCATION_ALIGNMENT;
    case KL_CHUNK:
      chunk = (kl_chunk_t*)alloc;
      return (size_t)CHUNK_HDR(chunk);
  }
  return 0;
}

static inline kl_alloc_t * KL_G_ALLOC(void const * const ptr)
{
  assert(KL_ISALIGNED(ptr));
  return (kl_alloc_t*)((uintptr_t)ptr-sizeof(uintptr_t));
}

static inline kl_alloc_t * KL_G_NEXT(void const * const addr)
{
  kl_alloc_t * alloc;

  alloc = (kl_alloc_t*)addr;

  assert(KL_ISALIGNED(ALLOC_PTR(alloc)));

  return (kl_alloc_t*)((uintptr_t)alloc+KL_G_SIZE(alloc));
}

static inline kl_alloc_t * KL_G_PREV(void const * const addr)
{
  uintptr_t off;
  kl_alloc_t * alloc;
  kl_brick_t * brick;
  kl_chunk_t * chunk;

  alloc = (kl_alloc_t*)addr;

  assert(KL_ISALIGNED(ALLOC_PTR(alloc)));

  switch (KL_TYPEOF(alloc)) {
    case KL_BRICK:
      brick = (kl_brick_t*)alloc;
      return (kl_alloc_t*)((uintptr_t)brick-KL_G_SIZE(alloc));
    case KL_CHUNK:
      chunk = (kl_chunk_t*)alloc;
      off   = *(uintptr_t*)((uintptr_t)chunk-sizeof(uintptr_t));
      return (kl_alloc_t*)((uintptr_t)chunk-off);
  }
  return 0;
}

static inline int KL_ISFULL(kl_fix_block_t const * const block)
{
  assert(KL_ISALIGNED((void*)block));
  return 0 == KL_G_COUNT(block);
}

static inline int KL_ISEMPTY(kl_fix_block_t const * const block)
{
  assert(KL_ISALIGNED((void*)block));
  return KL_G_COUNT(block) ==
    FIXED_MAX_SIZE/((KL_G_BIDX(block)+1)*MEMORY_ALLOCATION_ALIGNMENT);
}

static inline int KL_ISFIRST(kl_chunk_t const * const chunk)
{
  assert(KL_ISALIGNED(CHUNK_PTR(chunk)));

  return BLOCK_HEADER_ALIGN ==
    *(uintptr_t*)((uintptr_t)chunk-sizeof(uintptr_t));
}

static inline int KL_ISLAST(kl_chunk_t const * const chunk)
{
  assert(KL_ISALIGNED(CHUNK_PTR(chunk)));

  return BLOCK_HEADER_ALIGN == KL_G_SIZE(KL_G_NEXT((kl_alloc_t*)chunk));
}

static inline int KL_ISINUSE(kl_chunk_t const * const chunk)
{
  assert(KL_ISALIGNED(CHUNK_PTR(chunk)));

  return 0 == CHUNK_FTR(chunk);
}


/****************************************************************************/
/* =========================================================================*/
/****************************************************************************/


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
/* Free memory data structure */
/****************************************************************************/
#define UNDES_BIN_NUM 4
#define BRICK_BIN_NUM 256
#define CHUNK_BIN_NUM 1576

#define KL_ISSMALLBIN(B) ((B) < 1532)

#define KLSQR(V) ((V)*(V))

#define KL_SIZE2BIN(S)                                                      \
  (                                                                         \
    assert(0 != (S)),                                                       \
    ((S)<=64)                                                               \
      ? ((S)-1)/8                                                           \
      : (KLLOG2((S)-1)<20)                                                  \
        ? ((S)>=KLSQR(log2size[KLLOG2((S)-1)-1])+1)                         \
          ? log2off[KLLOG2((S)-1)] +                                        \
            ((S)-(KLSQR(log2size[KLLOG2((S)-1)-1])+1)) /                    \
            log2size[KLLOG2((S)-1)]                                         \
          : log2off[KLLOG2((S)-1)-1] +                                      \
            ((S)-(KLSQR(log2size[KLLOG2((S)-1)-2])+1)) /                    \
            log2size[KLLOG2((S)-1)-1]                                       \
        : log2off[KLLOG2((S)-1)]                                            \
  )

#define KL_BIN2SIZE(B) (assert(KL_ISSMALLBIN(B)), bin2size[(B)])

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

static size_t bin2size[1532]=
{
  8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208,
  224, 240, 256, 288, 320, 352, 384, 416, 448, 480, 512, 544, 576, 608, 640,
  672, 704, 736, 768, 800, 832, 864, 896, 928, 960, 992, 1024, 1088, 1152, 1216,
  1280, 1344, 1408, 1472, 1536, 1600, 1664, 1728, 1792, 1856, 1920, 1984, 2048,
  2112, 2176, 2240, 2304, 2368, 2432, 2496, 2560, 2624, 2688, 2752, 2816, 2880,
  2944, 3008, 3072, 3136, 3200, 3264, 3328, 3392, 3456, 3520, 3584, 3648, 3712,
  3776, 3840, 3904, 3968, 4032, 4096, 4224, 4352, 4480, 4608, 4736, 4864, 4992,
  5120, 5248, 5376, 5504, 5632, 5760, 5888, 6016, 6144, 6272, 6400, 6528, 6656,
  6784, 6912, 7040, 7168, 7296, 7424, 7552, 7680, 7808, 7936, 8064, 8192, 8320,
  8448, 8576, 8704, 8832, 8960, 9088, 9216, 9344, 9472, 9600, 9728, 9856, 9984,
  10112, 10240, 10368, 10496, 10624, 10752, 10880, 11008, 11136, 11264, 11392,
  11520, 11648, 11776, 11904, 12032, 12160, 12288, 12416, 12544, 12672, 12800,
  12928, 13056, 13184, 13312, 13440, 13568, 13696, 13824, 13952, 14080, 14208,
  14336, 14464, 14592, 14720, 14848, 14976, 15104, 15232, 15360, 15488, 15616,
  15744, 15872, 16000, 16128, 16256, 16384, 16640, 16896, 17152, 17408, 17664,
  17920, 18176, 18432, 18688, 18944, 19200, 19456, 19712, 19968, 20224, 20480,
  20736, 20992, 21248, 21504, 21760, 22016, 22272, 22528, 22784, 23040, 23296,
  23552, 23808, 24064, 24320, 24576, 24832, 25088, 25344, 25600, 25856, 26112,
  26368, 26624, 26880, 27136, 27392, 27648, 27904, 28160, 28416, 28672, 28928,
  29184, 29440, 29696, 29952, 30208, 30464, 30720, 30976, 31232, 31488, 31744,
  32000, 32256, 32512, 32768, 33024, 33280, 33536, 33792, 34048, 34304, 34560,
  34816, 35072, 35328, 35584, 35840, 36096, 36352, 36608, 36864, 37120, 37376,
  37632, 37888, 38144, 38400, 38656, 38912, 39168, 39424, 39680, 39936, 40192,
  40448, 40704, 40960, 41216, 41472, 41728, 41984, 42240, 42496, 42752, 43008,
  43264, 43520, 43776, 44032, 44288, 44544, 44800, 45056, 45312, 45568, 45824,
  46080, 46336, 46592, 46848, 47104, 47360, 47616, 47872, 48128, 48384, 48640,
  48896, 49152, 49408, 49664, 49920, 50176, 50432, 50688, 50944, 51200, 51456,
  51712, 51968, 52224, 52480, 52736, 52992, 53248, 53504, 53760, 54016, 54272,
  54528, 54784, 55040, 55296, 55552, 55808, 56064, 56320, 56576, 56832, 57088,
  57344, 57600, 57856, 58112, 58368, 58624, 58880, 59136, 59392, 59648, 59904,
  60160, 60416, 60672, 60928, 61184, 61440, 61696, 61952, 62208, 62464, 62720,
  62976, 63232, 63488, 63744, 64000, 64256, 64512, 64768, 65024, 65280, 65536,
  66048, 66560, 67072, 67584, 68096, 68608, 69120, 69632, 70144, 70656, 71168,
  71680, 72192, 72704, 73216, 73728, 74240, 74752, 75264, 75776, 76288, 76800,
  77312, 77824, 78336, 78848, 79360, 79872, 80384, 80896, 81408, 81920, 82432,
  82944, 83456, 83968, 84480, 84992, 85504, 86016, 86528, 87040, 87552, 88064,
  88576, 89088, 89600, 90112, 90624, 91136, 91648, 92160, 92672, 93184, 93696,
  94208, 94720, 95232, 95744, 96256, 96768, 97280, 97792, 98304, 98816, 99328,
  99840, 100352, 100864, 101376, 101888, 102400, 102912, 103424, 103936,
  104448, 104960, 105472, 105984, 106496, 107008, 107520, 108032, 108544,
  109056, 109568, 110080, 110592, 111104, 111616, 112128, 112640, 113152,
  113664, 114176, 114688, 115200, 115712, 116224, 116736, 117248, 117760,
  118272, 118784, 119296, 119808, 120320, 120832, 121344, 121856, 122368,
  122880, 123392, 123904, 124416, 124928, 125440, 125952, 126464, 126976,
  127488, 128000, 128512, 129024, 129536, 130048, 130560, 131072, 131584,
  132096, 132608, 133120, 133632, 134144, 134656, 135168, 135680, 136192,
  136704, 137216, 137728, 138240, 138752, 139264, 139776, 140288, 140800,
  141312, 141824, 142336, 142848, 143360, 143872, 144384, 144896, 145408,
  145920, 146432, 146944, 147456, 147968, 148480, 148992, 149504, 150016,
  150528, 151040, 151552, 152064, 152576, 153088, 153600, 154112, 154624,
  155136, 155648, 156160, 156672, 157184, 157696, 158208, 158720, 159232,
  159744, 160256, 160768, 161280, 161792, 162304, 162816, 163328, 163840,
  164352, 164864, 165376, 165888, 166400, 166912, 167424, 167936, 168448,
  168960, 169472, 169984, 170496, 171008, 171520, 172032, 172544, 173056,
  173568, 174080, 174592, 175104, 175616, 176128, 176640, 177152, 177664,
  178176, 178688, 179200, 179712, 180224, 180736, 181248, 181760, 182272,
  182784, 183296, 183808, 184320, 184832, 185344, 185856, 186368, 186880,
  187392, 187904, 188416, 188928, 189440, 189952, 190464, 190976, 191488,
  192000, 192512, 193024, 193536, 194048, 194560, 195072, 195584, 196096,
  196608, 197120, 197632, 198144, 198656, 199168, 199680, 200192, 200704,
  201216, 201728, 202240, 202752, 203264, 203776, 204288, 204800, 205312,
  205824, 206336, 206848, 207360, 207872, 208384, 208896, 209408, 209920,
  210432, 210944, 211456, 211968, 212480, 212992, 213504, 214016, 214528,
  215040, 215552, 216064, 216576, 217088, 217600, 218112, 218624, 219136,
  219648, 220160, 220672, 221184, 221696, 222208, 222720, 223232, 223744,
  224256, 224768, 225280, 225792, 226304, 226816, 227328, 227840, 228352,
  228864, 229376, 229888, 230400, 230912, 231424, 231936, 232448, 232960,
  233472, 233984, 234496, 235008, 235520, 236032, 236544, 237056, 237568,
  238080, 238592, 239104, 239616, 240128, 240640, 241152, 241664, 242176,
  242688, 243200, 243712, 244224, 244736, 245248, 245760, 246272, 246784,
  247296, 247808, 248320, 248832, 249344, 249856, 250368, 250880, 251392,
  251904, 252416, 252928, 253440, 253952, 254464, 254976, 255488, 256000,
  256512, 257024, 257536, 258048, 258560, 259072, 259584, 260096, 260608,
  261120, 261632, 262144, 263168, 264192, 265216, 266240, 267264, 268288,
  269312, 270336, 271360, 272384, 273408, 274432, 275456, 276480, 277504,
  278528, 279552, 280576, 281600, 282624, 283648, 284672, 285696, 286720,
  287744, 288768, 289792, 290816, 291840, 292864, 293888, 294912, 295936,
  296960, 297984, 299008, 300032, 301056, 302080, 303104, 304128, 305152,
  306176, 307200, 308224, 309248, 310272, 311296, 312320, 313344, 314368,
  315392, 316416, 317440, 318464, 319488, 320512, 321536, 322560, 323584,
  324608, 325632, 326656, 327680, 328704, 329728, 330752, 331776, 332800,
  333824, 334848, 335872, 336896, 337920, 338944, 339968, 340992, 342016,
  343040, 344064, 345088, 346112, 347136, 348160, 349184, 350208, 351232,
  352256, 353280, 354304, 355328, 356352, 357376, 358400, 359424, 360448,
  361472, 362496, 363520, 364544, 365568, 366592, 367616, 368640, 369664,
  370688, 371712, 372736, 373760, 374784, 375808, 376832, 377856, 378880,
  379904, 380928, 381952, 382976, 384000, 385024, 386048, 387072, 388096,
  389120, 390144, 391168, 392192, 393216, 394240, 395264, 396288, 397312,
  398336, 399360, 400384, 401408, 402432, 403456, 404480, 405504, 406528,
  407552, 408576, 409600, 410624, 411648, 412672, 413696, 414720, 415744,
  416768, 417792, 418816, 419840, 420864, 421888, 422912, 423936, 424960,
  425984, 427008, 428032, 429056, 430080, 431104, 432128, 433152, 434176,
  435200, 436224, 437248, 438272, 439296, 440320, 441344, 442368, 443392,
  444416, 445440, 446464, 447488, 448512, 449536, 450560, 451584, 452608,
  453632, 454656, 455680, 456704, 457728, 458752, 459776, 460800, 461824,
  462848, 463872, 464896, 465920, 466944, 467968, 468992, 470016, 471040,
  472064, 473088, 474112, 475136, 476160, 477184, 478208, 479232, 480256,
  481280, 482304, 483328, 484352, 485376, 486400, 487424, 488448, 489472,
  490496, 491520, 492544, 493568, 494592, 495616, 496640, 497664, 498688,
  499712, 500736, 501760, 502784, 503808, 504832, 505856, 506880, 507904,
  508928, 509952, 510976, 512000, 513024, 514048, 515072, 516096, 517120,
  518144, 519168, 520192, 521216, 522240, 523264, 524288, 525312, 526336,
  527360, 528384, 529408, 530432, 531456, 532480, 533504, 534528, 535552,
  536576, 537600, 538624, 539648, 540672, 541696, 542720, 543744, 544768,
  545792, 546816, 547840, 548864, 549888, 550912, 551936, 552960, 553984,
  555008, 556032, 557056, 558080, 559104, 560128, 561152, 562176, 563200,
  564224, 565248, 566272, 567296, 568320, 569344, 570368, 571392, 572416,
  573440, 574464, 575488, 576512, 577536, 578560, 579584, 580608, 581632,
  582656, 583680, 584704, 585728, 586752, 587776, 588800, 589824, 590848,
  591872, 592896, 593920, 594944, 595968, 596992, 598016, 599040, 600064,
  601088, 602112, 603136, 604160, 605184, 606208, 607232, 608256, 609280,
  610304, 611328, 612352, 613376, 614400, 615424, 616448, 617472, 618496,
  619520, 620544, 621568, 622592, 623616, 624640, 625664, 626688, 627712,
  628736, 629760, 630784, 631808, 632832, 633856, 634880, 635904, 636928,
  637952, 638976, 640000, 641024, 642048, 643072, 644096, 645120, 646144,
  647168, 648192, 649216, 650240, 651264, 652288, 653312, 654336, 655360,
  656384, 657408, 658432, 659456, 660480, 661504, 662528, 663552, 664576,
  665600, 666624, 667648, 668672, 669696, 670720, 671744, 672768, 673792,
  674816, 675840, 676864, 677888, 678912, 679936, 680960, 681984, 683008,
  684032, 685056, 686080, 687104, 688128, 689152, 690176, 691200, 692224,
  693248, 694272, 695296, 696320, 697344, 698368, 699392, 700416, 701440,
  702464, 703488, 704512, 705536, 706560, 707584, 708608, 709632, 710656,
  711680, 712704, 713728, 714752, 715776, 716800, 717824, 718848, 719872,
  720896, 721920, 722944, 723968, 724992, 726016, 727040, 728064, 729088,
  730112, 731136, 732160, 733184, 734208, 735232, 736256, 737280, 738304,
  739328, 740352, 741376, 742400, 743424, 744448, 745472, 746496, 747520,
  748544, 749568, 750592, 751616, 752640, 753664, 754688, 755712, 756736,
  757760, 758784, 759808, 760832, 761856, 762880, 763904, 764928, 765952,
  766976, 768000, 769024, 770048, 771072, 772096, 773120, 774144, 775168,
  776192, 777216, 778240, 779264, 780288, 781312, 782336, 783360, 784384,
  785408, 786432, 787456, 788480, 789504, 790528, 791552, 792576, 793600,
  794624, 795648, 796672, 797696, 798720, 799744, 800768, 801792, 802816,
  803840, 804864, 805888, 806912, 807936, 808960, 809984, 811008, 812032,
  813056, 814080, 815104, 816128, 817152, 818176, 819200, 820224, 821248,
  822272, 823296, 824320, 825344, 826368, 827392, 828416, 829440, 830464,
  831488, 832512, 833536, 834560, 835584, 836608, 837632, 838656, 839680,
  840704, 841728, 842752, 843776, 844800, 845824, 846848, 847872, 848896,
  849920, 850944, 851968, 852992, 854016, 855040, 856064, 857088, 858112,
  859136, 860160, 861184, 862208, 863232, 864256, 865280, 866304, 867328,
  868352, 869376, 870400, 871424, 872448, 873472, 874496, 875520, 876544,
  877568, 878592, 879616, 880640, 881664, 882688, 883712, 884736, 885760,
  886784, 887808, 888832, 889856, 890880, 891904, 892928, 893952, 894976,
  896000, 897024, 898048, 899072, 900096, 901120, 902144, 903168, 904192,
  905216, 906240, 907264, 908288, 909312, 910336, 911360, 912384, 913408,
  914432, 915456, 916480, 917504, 918528, 919552, 920576, 921600, 922624,
  923648, 924672, 925696, 926720, 927744, 928768, 929792, 930816, 931840,
  932864, 933888, 934912, 935936, 936960, 937984, 939008, 940032, 941056,
  942080, 943104, 944128, 945152, 946176, 947200, 948224, 949248, 950272,
  951296, 952320, 953344, 954368, 955392, 956416, 957440, 958464, 959488,
  960512, 961536, 962560, 963584, 964608, 965632, 966656, 967680, 968704,
  969728, 970752, 971776, 972800, 973824, 974848, 975872, 976896, 977920,
  978944, 979968, 980992, 982016, 983040, 984064, 985088, 986112, 987136,
  988160, 989184, 990208, 991232, 992256, 993280, 994304, 995328, 996352,
  997376, 998400, 999424, 1000448, 1001472, 1002496, 1003520, 1004544, 1005568,
  1006592, 1007616, 1008640, 1009664, 1010688, 1011712, 1012736, 1013760,
  1014784, 1015808, 1016832, 1017856, 1018880, 1019904, 1020928, 1021952,
  1022976, 1024000, 1025024, 1026048, 1027072, 1028096, 1029120, 1030144,
  1031168, 1032192, 1033216, 1034240, 1035264, 1036288, 1037312, 1038336,
  1039360, 1040384, 1041408, 1042432, 1043456, 1044480, 1045504, 1046528,
  1047552, 1048576
};



/****************************************************************************/
/* Free memory data structure */
/****************************************************************************/
typedef struct kl_mem
{
  int init;
  int enabled;

  size_t sys_ctr;
  size_t mem_total;
  size_t mem_max;

  size_t mem_brick_cur; /* bytes of in use space for bricks */
  size_t mem_brick_tot; /* bytes of allocated space for bricks */
  size_t mem_chunk_cur; /* bytes of in use space for chunks */
  size_t mem_chunk_tot; /* bytes of allocated space for chunks */

  size_t num_undes;
  kl_fix_block_t * undes_bin[UNDES_BIN_NUM];
  kl_fix_block_t * brick_bin[BRICK_BIN_NUM];
  kl_chunk_t * chunk_bin[CHUNK_BIN_NUM];

#ifdef USE_PTHREAD
  pthread_mutex_t init_lock;  /* mutex guarding initialization */
  pthread_mutex_t lock;       /* mutex guarding struct */
#endif
} kl_mem_t;

static kl_mem_t mem={
#ifdef USE_PTHREAD
  .init_lock = PTHREAD_MUTEX_INITIALIZER,
#endif
  .init = 0,
  .enabled = M_ENABLED_OFF
};

static int
kl_mem_init(kl_mem_t * const mem)
{
  int i;

  GET_LOCK(&(mem->init_lock));

  if (1 == mem->init)
    goto DONE;

  mem->init          = 1;
  mem->sys_ctr       = 0;
  mem->mem_total     = 0;
  mem->mem_max       = 0;
  mem->num_undes     = 0;
  mem->mem_brick_cur = 0;
  mem->mem_brick_tot = 0;
  mem->mem_chunk_cur = 0;
  mem->mem_chunk_tot = 0;

  for (i=0; i<UNDES_BIN_NUM; ++i)
    mem->undes_bin[i] = NULL;
  for (i=0; i<BRICK_BIN_NUM; ++i)
    mem->brick_bin[i] = NULL;
  for (i=0; i<CHUNK_BIN_NUM; ++i)
    mem->chunk_bin[i] = NULL;

  INIT_LOCK(&(mem->lock));

  DONE:
  LET_LOCK(&(mem->init_lock));

  return 0;
}


/****************************************************************************/
/* Initialize free chunk data structure */
/****************************************************************************/
static void
kl_mem_destroy(kl_mem_t * const mem)
{
  size_t i;

  GET_LOCK(&(mem->init_lock));

  if (0 == mem->init)
    goto DONE;

  //printf("[%5d] %s:%d\n", (int)getpid(), basename(__FILE__), __LINE__);
  for (i=0; i<mem->num_undes; ++i) {
    /* Release back to system. */
    CALL_SYS_FREE(mem->undes_bin[i], BLOCK_DEFAULT_SIZE);

    /* Accounting. */
    mem->mem_total -= BLOCK_DEFAULT_SIZE;
  }
  mem->init = 0;
  //printf("[%5d] %s:%d\n", (int)getpid(), basename(__FILE__), __LINE__);

  FREE_LOCK(&(mem->lock));
  //printf("[%5d] %s:%d\n", (int)getpid(), basename(__FILE__), __LINE__);

  DONE:
  LET_LOCK(&(mem->init_lock));
}


/****************************************************************************/
/* =========================================================================*/
/****************************************************************************/


/****************************************************************************/
/* Allocate a new block */
/****************************************************************************/
static void *
kl_block_alloc(kl_mem_t * const mem, size_t const size)
{
  void * ret, * block=NULL;

  /* Get system memory. */
  ret = CALL_SYS_ALLOC(block, size);
  if (SYS_ALLOC_FAIL == ret)
    return NULL;
  CALL_SYS_BZERO(block, size);

  GET_LOCK(&(mem->lock));

  /* Accounting. */
  mem->mem_total += size;
  if (mem->mem_total > mem->mem_max)
    mem->mem_max = mem->mem_total;
  mem->sys_ctr++;

  LET_LOCK(&(mem->lock));

  return block;
}


#ifdef CALL_SYS_REALLOC
/****************************************************************************/
/* Reallocate a existing block (only for increasing size) */
/****************************************************************************/
static void *
kl_block_realloc(kl_mem_t * const mem, void * const oblock,
                 size_t const osize, size_t const nsize)
{
  void * ret, * nblock;

  /* Get system memory. */
  ret = CALL_SYS_REALLOC(nblock, oblock, osize, nsize);
  if (SYS_ALLOC_FAIL == ret)
    return NULL;
  CALL_SYS_BZERO(nblock, nsize);

  GET_LOCK(&(mem->lock));

  /* Accounting. */
  mem->mem_total += (nsize-osize);
  if (mem->mem_total > mem->mem_max)
    mem->mem_max = mem->mem_total;
  mem->sys_ctr++;

  LET_LOCK(&(mem->lock));

  return nblock;
}
#endif


/****************************************************************************/
/* Add a node to a free brick data structure */
/****************************************************************************/
static int
kl_brick_put(kl_mem_t * const mem, kl_brick_t * const brick)
{
  size_t bidx;
  kl_fix_block_t * block;

  GET_LOCK(&(mem->lock));

  block = (kl_fix_block_t*)KL_G_BLOCK((kl_alloc_t*)brick);
  bidx  = KL_G_BIDX(block);

  /* Increment block count.  This is safe (won't affect multiplier) because
   * the count occupies the low bytes of the header. */
  BLOCK_HDR(block)++;

  if (KL_ISEMPTY(block)) {
    /* Remove from doubly-linked list of blocks in bin[bidx]. */
    if (NULL == BLOCK_PREV(block))
      mem->brick_bin[bidx] = BLOCK_NEXT(block);
    else
      BLOCK_NEXT(BLOCK_PREV(block)) = BLOCK_NEXT(block);
    if (NULL != BLOCK_NEXT(block))
      BLOCK_PREV(BLOCK_NEXT(block)) = BLOCK_PREV(block);

    if (mem->num_undes < UNDES_BIN_NUM) {
      BLOCK_HDR(block)  = 0;
      BLOCK_PREV(block) = NULL;
      BLOCK_NEXT(block) = NULL;
      BLOCK_HEAD(block) = NULL;
      //memset(block, 0, BLOCK_DEFAULT_SIZE);

      /* Put block on undesignated stack. */
      mem->undes_bin[mem->num_undes++] = block;
    }
    else {
      /* Release back to system. */
      CALL_SYS_FREE(block, BLOCK_DEFAULT_SIZE);

      /* Accounting. */
      mem->mem_total -= BLOCK_DEFAULT_SIZE;
    }
  }
  else {
    /* Prepend brick to front of block singly-linked list. */
    BRICK_NEXT(brick) = BLOCK_HEAD(block);
    BLOCK_HEAD(block) = brick;

    /* If not already part of doubly-linked list, add block to it. */
    if (NULL == BLOCK_PREV(block) && NULL == BLOCK_NEXT(block) &&
        block != mem->brick_bin[bidx])
    {
      BLOCK_PREV(block) = NULL;
      BLOCK_NEXT(block) = mem->brick_bin[bidx];
      if (NULL != BLOCK_NEXT(block))
        BLOCK_PREV(BLOCK_NEXT(block)) = block;
      mem->brick_bin[bidx] = block;
    }
  }

  LET_LOCK(&(mem->lock));
  return 0;
}


/****************************************************************************/
/* Find the brick with the smallest size >= size parameter */
/****************************************************************************/
static void *
kl_brick_get(kl_mem_t * const mem, size_t const size)
{
  size_t bidx;
  kl_fix_block_t * block;
  kl_brick_t * brick;

  if (size > BRICK_MAX_SIZE)
    return NULL;

  GET_LOCK(&(mem->lock));

  bidx  = KL_G_BRICKBIN(size);
  block = mem->brick_bin[bidx];

  if (NULL == block || NULL == BLOCK_HEAD(block)) {
    if (0 != mem->num_undes) {            /* Designate existing block. */
      block = mem->undes_bin[--mem->num_undes];
    }
    else {                                /* Allocate new block. */
      block = (kl_fix_block_t*)kl_block_alloc(mem, BLOCK_DEFAULT_SIZE);
      if (NULL == block)
        goto FAILURE;
    }

    /* Set block bin index (multiplier-1). */
    BLOCK_HDR(block) = bidx<<BLOCK_BIDX_SHIFT;
    /* Set block count. */
    BLOCK_HDR(block) |= (FIXED_MAX_SIZE/((bidx+1)*MEMORY_ALLOCATION_ALIGNMENT));

    /* Set block as head of doubly-linked list. */
    BLOCK_PREV(block) = NULL;
    BLOCK_NEXT(block) = mem->brick_bin[bidx];
    if (NULL != BLOCK_NEXT(block))
      BLOCK_PREV(BLOCK_NEXT(block)) = block;
    mem->brick_bin[bidx] = block;

    /* Set head of block's singly-linked list to first brick. */
    BLOCK_HEAD(block) = (kl_brick_t*)BLOCK_PTR(block);

    /* Reset pointer for head brick.  This way, dangling pointers will get
     * reset correctly.  See note in the if(0 == BRICK_HDR(brick)) condition
     * below. */
    BRICK_HDR(BLOCK_HEAD(block)) = 0;
  }
  else {                                  /* Use head block. */
  }

  /* Decrement block count.  This is safe (won't affect multiplier) because
   * the count occupies the low bytes of the header. */
  BLOCK_HDR(block)--;

  /* Remove full block from doubly-linked list. */
  if (KL_ISFULL(block)) {
    if (NULL == BLOCK_PREV(block))
      mem->brick_bin[bidx] = BLOCK_NEXT(block);
    else
      BLOCK_NEXT(BLOCK_PREV(block)) = BLOCK_NEXT(block);
    if (NULL != BLOCK_NEXT(block))
      BLOCK_PREV(BLOCK_NEXT(block)) = BLOCK_PREV(block);

    BLOCK_PREV(block) = NULL;
    BLOCK_NEXT(block) = NULL;
  }

  /* Get brick. */
  brick = BLOCK_HEAD(block);

  /* Check if brick has never been previously used. */
  if (0 == BRICK_HDR(brick)) {
    /* Set block pointer. */
    BRICK_HDR(brick) = (uintptr_t)block|KL_BRICK;

    /* Check if block is not full. */
    if (!KL_ISFULL(block)) {
      /* Set next pointer. */
      BRICK_NEXT(brick) = (kl_brick_t*)KL_G_NEXT((kl_alloc_t*)brick);

      /* Reset pointer for next brick.  If this brick has never been
       * previously used, then if follows that neither has the next.  Reset
       * pointers in this way makes it so that memset'ing a block when it is
       * undesignated unnecessary. */
      BRICK_HDR(BRICK_NEXT(brick)) = 0;
    }
  }

  /* Remove brick from front of block's singly-linked list. */
  BLOCK_HEAD(block) = BRICK_NEXT(brick);
  BRICK_NEXT(brick) = NULL;

  LET_LOCK(&(mem->lock));
  return brick;

  FAILURE:
  LET_LOCK(&(mem->lock));
  return NULL;
}


/****************************************************************************/
/* =========================================================================*/
/****************************************************************************/


/****************************************************************************/
/* Remove a node from a free chunk data structure */
/****************************************************************************/
static int
kl_chunk_del(kl_mem_t * const mem, kl_chunk_t * const chunk)
{
  size_t bidx;

  GET_LOCK(&(mem->lock));

  assert(KL_G_SIZE(chunk) >= CHUNK_MIN_SIZE);

  bidx = KL_SIZE2BIN(KL_G_SIZE(chunk));

  /* Shift bin index in case a chunk is made up of coalesced chunks which
   * collectively have a size which causes the chunk to fall in a particular
   * bin, but has less bytes than required by the bin. */
  if (CHUNK_HDR(chunk) < KL_BIN2SIZE(bidx) && bidx > 0)
    bidx--;

  /* Fixed and variable sized bins are treated the same, since removing a node
   * from a variable sized bin will not cause it to become unsorted. */
  if (NULL == CHUNK_PREV(chunk))
    mem->chunk_bin[bidx] = CHUNK_NEXT(chunk);
  else
    CHUNK_NEXT(CHUNK_PREV(chunk)) = CHUNK_NEXT(chunk);
  if (NULL != CHUNK_NEXT(chunk))
    CHUNK_PREV(CHUNK_NEXT(chunk)) = CHUNK_PREV(chunk);
  CHUNK_PREV(chunk) = NULL;
  CHUNK_NEXT(chunk) = NULL;

  LET_LOCK(&(mem->lock));
  return 0;
}


/****************************************************************************/
/* Add a node to a free chunk data structure */
/****************************************************************************/
static int
kl_chunk_put(kl_mem_t * const mem, kl_chunk_t * chunk)
{
  size_t bidx;
  void * block;
  kl_chunk_t * prev, * next;

  GET_LOCK(&(mem->lock));

  /* Sanity check: chunk size is still valid. */
  assert(KL_G_SIZE(chunk) >= CHUNK_MIN_SIZE);

  if (!KL_ISFIRST(chunk)) {
    prev = (kl_chunk_t*)KL_G_PREV(chunk);

    /* Coalesce with previous chunk. */
    if (prev != chunk && !KL_ISINUSE(prev)) {
      /* Remove previous chunk from free chunk data structure. */
      if (0 != kl_chunk_del(mem, prev))
        goto FAILURE;

      /* Update chunk size. */
      CHUNK_HDR(prev) += CHUNK_HDR(chunk);

      /* Set chunk to point to previous chunk. */
      chunk = prev;
    }
  }

  if (!KL_ISLAST(chunk)) {
    next = (kl_chunk_t*)KL_G_NEXT(chunk);

    /* Coalesce with following chunk. */
    if (!KL_ISINUSE(next)) {
      /* Remove following chunk from free chunk data structure. */
      if (0 != kl_chunk_del(mem, next))
        goto FAILURE;

      /* Update chunk size. */
      CHUNK_HDR(chunk) += CHUNK_HDR(next);
    }
  }

  /* Sanity check: chunk size is still valid. */
  assert(KL_G_SIZE(chunk) >= CHUNK_MIN_SIZE);

  /* If chunk is the only chunk, release memory back to system. */
  if (KL_ISFIRST(chunk) && KL_ISLAST(chunk)) {
    block = KL_G_PREV(chunk);

    if (mem->num_undes < UNDES_BIN_NUM &&
        BLOCK_DEFAULT_SIZE == KL_BLOCK_SIZE(KL_G_SIZE(chunk)))
    {
      /* need to zero out memory, so that there are no dangling pointers. */
      memset(block, 0, BLOCK_DEFAULT_SIZE);

      /* Put block on undesignated stack. */
      mem->undes_bin[mem->num_undes++] = block;
    }
    else {
      /* Accounting. */
      mem->mem_total -= KL_BLOCK_SIZE(KL_G_SIZE(chunk));

      CALL_SYS_FREE(block, KL_BLOCK_SIZE(KL_G_SIZE(chunk)));
    }
  }
  else {
    /* Set chunk as not in use. */
    CHUNK_FTR(chunk) = CHUNK_HDR(chunk);

    /* Get bin index for chunk. */
    bidx = KL_SIZE2BIN(KL_G_SIZE(chunk));

    /* Treat fixed size bins and large bins differently */
    if (KL_ISSMALLBIN(bidx)) {
      /* Shift bin index in case a chunk is made up of coalesced chunks which
       * collectively have a size which causes the chunk to fall in a particular
       * bin, but has less bytes than required by the bin. */
      if (KL_G_SIZE(chunk) < KL_BIN2SIZE(bidx) && bidx > 0)
        bidx--;

      /* Sanity check: chunk must be at least the size of the fixed bin. */
      assert(KL_G_SIZE(chunk) >= KL_BIN2SIZE(bidx));

      /* Prepend n to front of chunk_bin[bidx] linked-list. */
      CHUNK_PREV(chunk) = NULL;
      CHUNK_NEXT(chunk) = mem->chunk_bin[bidx];
      if (NULL != mem->chunk_bin[bidx])
        CHUNK_PREV(mem->chunk_bin[bidx]) = chunk;
      mem->chunk_bin[bidx] = chunk;
    }
#if 0
    else {
      /* This will keep large buckets sorted. */
      n = mem->chunk_bin[bidx];
      p = NULL;

      while (NULL != n && *KL_P2H(n) < *KL_C2H(chunk)) {
        p = n;
        n = n->n;
      }

      if (NULL != n) {
        /* insert internally */
        node->p = n->p;
        node->n = n;
        if (NULL == n->p)
          mem->chunk_bin[bidx] = node;
        else
          n->p->n = node;
        n->p = node;
      }
      else if (NULL != p) {
        /* insert at the end */
        p->n = node;
        node->p = p;
        node->n = NULL;
      }
      else {
        /* insert at the beginning */
        mem->chunk_bin[bidx] = node;
        node->p = NULL;
        node->n = NULL;
      }
    }
#endif
  }

  LET_LOCK(&(mem->lock));
  return 0;

  FAILURE:
  LET_LOCK(&(mem->lock));
  return -1;
}


/****************************************************************************/
/* Find the chunk with the smallest size >= size parameter */
/****************************************************************************/
static kl_chunk_t *
kl_chunk_get(kl_mem_t * const mem, size_t const size)
{
  size_t bidx, chunk_size;
  kl_fix_block_t * block;
  kl_chunk_t * chunk=NULL, * next;

  if (KL_CHUNK_SIZE(size) > FIXED_MAX_SIZE)
    return NULL;

  GET_LOCK(&(mem->lock));

  bidx = KL_SIZE2BIN(KL_CHUNK_SIZE(size));

  if (KL_ISSMALLBIN(bidx)) {
    /* Find first chunk bin with a node. */
    chunk = mem->chunk_bin[bidx];
    while (NULL == chunk && KL_ISSMALLBIN(bidx))
      chunk = mem->chunk_bin[++bidx];

    if (NULL != chunk) {                /* Use chunk from existing block. */
      /* Remove head of chunk_bin[bidx]. */
      mem->chunk_bin[bidx] = CHUNK_NEXT(chunk);
      if (NULL != mem->chunk_bin[bidx])
        CHUNK_PREV(mem->chunk_bin[bidx]) = NULL;
      CHUNK_NEXT(chunk) = NULL;
    }
    else {
      if (0 != mem->num_undes) {          /* Designate existing block. */
        block = mem->undes_bin[--mem->num_undes];
      }
      else {                              /* Allocate new block. */
        block = (kl_fix_block_t*)kl_block_alloc(mem, BLOCK_DEFAULT_SIZE);
        if (NULL == block)
          goto FAILURE;
      }

      /* Set block header and footer size. */
      BLOCK_HDR(block) = BLOCK_HEADER_ALIGN;
      BLOCK_FTR(block, BLOCK_DEFAULT_SIZE) = BLOCK_HDR(block);

      /* Set the chunk to be returned. */
      chunk = (kl_chunk_t*)BLOCK_PTR(block);

      /* Set chunk header and footer size (set chunk as not in use). */
      CHUNK_HDR(chunk) = FIXED_MAX_SIZE;
      CHUNK_FTR(chunk) = CHUNK_HDR(chunk);
    }

    /* Split chunk if applicable. */
    if (CHUNK_HDR(chunk) > CHUNK_MIN_SIZE &&
        KL_CHUNK_SIZE(size) <= CHUNK_HDR(chunk)-CHUNK_MIN_SIZE)
    {
      /* Get current chunk[0] size. */
      chunk_size = CHUNK_HDR(chunk);

      /* Update chunk[0] header and footer (in use). */
      CHUNK_HDR(chunk) = KL_CHUNK_SIZE(size);
      CHUNK_FTR(chunk) = 0;

      /* Set chunk[1] header and footer (not in use). */
      next = (kl_chunk_t*)KL_G_NEXT(chunk);
      CHUNK_HDR(next)  = chunk_size-CHUNK_HDR(chunk);
      CHUNK_FTR(next)  = CHUNK_HDR(next);
      CHUNK_PREV(next) = NULL;
      CHUNK_NEXT(next) = NULL;

      if (0 != kl_chunk_put(mem, next))
        goto FAILURE;
    }
    else {
      /* Set chunk[0] as in use */
      CHUNK_FTR(chunk) = 0;
    }
  }
#if 0
  else {
    /* Find first chunk_bin with a node. */
    chunk = mem->chunk_bin[bidx];
    while (NULL == chunk && bidx < CHUNK_BIN_NUM)
      chunk = mem->chunk_bin[++bidx];

    /* Find first node in chunk_bin[bidx] with size >= size parameter. */
    while (NULL != chunk && KL_G_SIZE(chunk) < size)
      chunk = CHUNK_NEXT(chunk);

    if (NULL != chunk) {                /* Use chunk from existing block. */
      /* Remove chunk from chunk_bin[bidx]. */
      if (NULL == CHUNK_PREV(chunk)) {
        mem->chunk_bin[bidx] = CHUNK_NEXT(chunk);
        if (NULL != mem->chunk_bin[bidx])
          CHUNK_PREV(mem->chunk_bin[bidx]) = NULL;
      }
      else {
        CHUNK_NEXT(CHUNK_PREV(chunk)) = CHUNK_NEXT(chunk);
        if (NULL != CHUNK_NEXT(chunk))
          CHUNK_PREV(CHUNK_NEXT(chunk)) = CHUNK_PREV(chunk);
      }
      CHUNK_PREV(chunk) = NULL;
      CHUNK_NEXT(chunk) = NULL;
    }
    else {                              /* Allocate new block. */
      /* Acquire memory. */
      block = (kl_fix_block_t*)kl_block_alloc(mem, KL_BLOCK_SIZE(size));
      if (NULL == block)
        return NULL;

      /* Set block header and footer size. */
      BLOCK_HDR(block) = BLOCK_HEADER_ALIGN;
      BLOCK_FTR(block, BLOCK_DEFAULT_SIZE) = BLOCK_HDR(block);

      /* Set the chunk to be returned. */
      chunk = (kl_chunk_t*)BLOCK_PTR(block);

      /* Set chunk header and footer size (set chunk as not in use). */
      CHUNK_HDR(chunk) = FIXED_MAX_SIZE;
      CHUNK_FTR(chunk) = CHUNK_HDR(chunk);

    }
  }
#endif

  LET_LOCK(&(mem->lock));
  return chunk;

  FAILURE:
  LET_LOCK(&(mem->lock));
  return NULL;
}


/****************************************************************************/
/* Allocate a singular chunk */
/****************************************************************************/
static kl_chunk_t *
kl_chunk_solo(kl_mem_t * const mem, size_t const size)
{
  size_t block_size;
  kl_var_block_t * block;
  kl_chunk_t * chunk;

  GET_LOCK(&(mem->lock));

  /* Determine appropriate allocation size. */
  block_size = KL_BLOCK_SIZE(KL_CHUNK_SIZE(size));
  assert(block_size > BLOCK_DEFAULT_SIZE);

  block = (kl_var_block_t*)kl_block_alloc(mem, block_size);
  if (NULL == block)
    goto FAILURE;

  /* Set block header and footer size. */
  BLOCK_HDR(block) = BLOCK_HEADER_ALIGN;
  BLOCK_FTR(block, block_size) = BLOCK_HDR(block);

  /* Set the chunk to be returned. */
  chunk = (kl_chunk_t*)BLOCK_PTR(block);

  /* Set chunk header and footer size (set chunk as in use). */
  CHUNK_HDR(chunk) = KL_CHUNK_SIZE(size);
  CHUNK_FTR(chunk) = 0;

  /* Sanity check. */
  assert(block_size == KL_BLOCK_SIZE(KL_G_SIZE(chunk)));
  assert(block_size == KL_BLOCK_SIZE(KL_CHUNK_SIZE(size)));

  LET_LOCK(&(mem->lock));
  return chunk;

  FAILURE:
  LET_LOCK(&(mem->lock));
  return NULL;
}


/****************************************************************************/
/* =========================================================================*/
/****************************************************************************/


/****************************************************************************/
/* Return max size of brick. */
/****************************************************************************/
KL_EXPORT size_t
KL_brick_max_size(void)
{
  return BRICK_MAX_SIZE;
}


/****************************************************************************/
/* Return max size of fixed size chunk. */
/****************************************************************************/
KL_EXPORT size_t
KL_chunk_max_size(void)
{
  return FIXED_MAX_SIZE;
}


/****************************************************************************/
/* Return max size of solo chunk. */
/****************************************************************************/
KL_EXPORT size_t
KL_solo_max_size(void)
{
  return CHUNK_MAX_SIZE;
}


/****************************************************************************/
/* Allocate size bytes of memory */
/****************************************************************************/
KL_EXPORT void *
KL_malloc(size_t const size)
{
  void * ptr=NULL;
  kl_brick_t * brick;
  kl_chunk_t * chunk;

  /*
      Basic algorithm:
        If a small request (<= BRICK_MAX_SIZE bytes):
          1. If one exists, use a fixed size memory brick
          2. If available, designate existing unsed block for bricks, and use
             the first
          3. If available, create a new block and designate for bricks, and
             use the first
        If medium request (<= BLOCK_DEFAULT_SIZE-CHUNK_META_SIZE):
          1. If one exists, use a fixed size memory chunk, splitting if
             possible
          2. If available, designate existing unsed block for chunks, and use
             the first, splitting if possible
          3. If available, create a new block and designate for chunks, and
             use the first, splitting if possible
        Otherwise, for a large request (<= CHUNK_MAX_SIZE):
          1. If available, create a new block and designate for chunks, and
             use the first, do not split
  */

  /* Enabled check. */
  GET_LOCK(&(mem.init_lock));
  if (M_ENABLED_ON != mem.enabled) {
    LET_LOCK(&(mem.init_lock));
    return libc_malloc(size);
  }
  LET_LOCK(&(mem.init_lock));

  if (size > ALLOC_MAX_SIZE)
    return NULL;

  if (NULL != (brick=kl_brick_get(&mem, size))) {
    ptr = BRICK_PTR(brick);

    assert(size <= BRICK_MAX_SIZE);
    assert(KL_BRICK_SIZE(size) <= KL_G_SIZE(brick));
    assert(KL_BRICK == KL_TYPEOF(brick));
  }
  else if (NULL != (chunk=kl_chunk_get(&mem, size))) {
    ptr = CHUNK_PTR(chunk);

    assert(size <= FIXED_MAX_SIZE);
    assert(size <= CHUNK_MAX_SIZE);
    assert(KL_CHUNK_SIZE(size) <= KL_G_SIZE(chunk));
    assert(KL_CHUNK == KL_TYPEOF(chunk));
  }
  else if (NULL != (chunk=kl_chunk_solo(&mem, size))) {
    ptr = CHUNK_PTR(chunk);

    assert(size > FIXED_MAX_SIZE);
    assert(size <= CHUNK_MAX_SIZE);
    //assert(KL_CHUNK_SIZE(size) <= KL_G_SIZE(chunk));
    assert(KL_CHUNK_SIZE(size) == KL_G_SIZE(chunk));
    assert(KL_CHUNK == KL_TYPEOF(chunk));
  }

  assert(KL_ISALIGNED(ptr));
  assert(0 != KL_G_SIZE(KL_G_ALLOC(ptr)));

  return ptr;
}


/****************************************************************************/
/* Allocate zeroed memory */
/****************************************************************************/
KL_EXPORT void *
KL_calloc(size_t const num, size_t const size)
{
  void * ptr;

  /* Enabled check. */
  GET_LOCK(&(mem.init_lock));
  if (M_ENABLED_ON != mem.enabled) {
    LET_LOCK(&(mem.init_lock));
    return libc_calloc(num, size);
  }
  LET_LOCK(&(mem.init_lock));

  if (NULL == (ptr=KL_malloc(num*size)))
    return NULL;
  memset(ptr, 0, num*size);

  assert(0 != KL_G_SIZE(KL_G_ALLOC(ptr)));

  return ptr;
}


/****************************************************************************/
/* Re-allocate size bytes of memory with pointer hint */
/****************************************************************************/
KL_EXPORT void *
KL_realloc(void * const ptr, size_t const size)
{
  int ret;
  size_t osize, block_size, off;
  void * nptr;
  kl_chunk_t * chunk;
  kl_var_block_t * block;

  /* Enabled check. */
  GET_LOCK(&(mem.init_lock));
  if (M_ENABLED_ON != mem.enabled) {
    LET_LOCK(&(mem.init_lock));
    return libc_realloc(ptr, size);
  }
  LET_LOCK(&(mem.init_lock));

  chunk = (kl_chunk_t*)KL_G_ALLOC(ptr);
  osize = KL_G_SIZE(chunk);
  assert(0 != osize);

  /* See if current allocation is large enough. */
  if (KL_BRICK_SIZE(size) <= osize)
    return ptr;
  else if (KL_CHUNK_SIZE(size) <= osize)
    return ptr;

#ifdef CALL_SYS_REALLOC
  if (osize > FIXED_MAX_SIZE) {
    GET_LOCK(&(mem.lock));

    block = (kl_var_block_t*)KL_G_PREV(chunk);
    assert(BLOCK_HEADER_ALIGN == BLOCK_HDR(block));
    assert(BLOCK_HEADER_ALIGN == BLOCK_FTR(block, KL_BLOCK_SIZE(osize)));

    /* Determine appropriate allocation size. */
    block_size = KL_BLOCK_SIZE(KL_CHUNK_SIZE(size));
    assert(block_size > BLOCK_DEFAULT_SIZE);

    block = (kl_var_block_t*)kl_block_realloc(&mem, block,\
      KL_BLOCK_SIZE(osize), block_size);
    if (NULL == block) {
      LET_LOCK(&(mem.lock));
      goto MALLOC;
    }

    /* Set block header and footer size. */
    BLOCK_HDR(block) = BLOCK_HEADER_ALIGN;
    BLOCK_FTR(block, block_size) = BLOCK_HDR(block);

    /* Set the chunk to be returned. */
    chunk = (kl_chunk_t*)BLOCK_PTR(block);

    /* Set chunk header and footer size (set chunk as in use). */
    CHUNK_HDR(chunk) = KL_CHUNK_SIZE(size);
    CHUNK_FTR(chunk) = 0;

    /* Sanity check. */
    assert(KL_CHUNK_SIZE(size) == KL_G_SIZE(chunk));
    assert(block_size == KL_BLOCK_SIZE(KL_G_SIZE(chunk)));
    assert(block_size == KL_BLOCK_SIZE(KL_CHUNK_SIZE(size)));

    LET_LOCK(&(mem.lock));

    return CHUNK_PTR(chunk);
  }

  MALLOC:
#endif
  /* Allocate new, larger region of memory. */
  if (NULL == (nptr=KL_malloc(size)))
    return NULL;
  assert(osize <= KL_G_SIZE(KL_G_ALLOC(nptr)));

#ifdef CALL_SYS_REMAP
  if (osize > FIXED_MAX_SIZE) {
    off = (uintptr_t)ptr-(uintptr_t)KL_G_PREV(KL_G_ALLOC(ptr));
    assert(off == (uintptr_t)nptr-(uintptr_t)KL_G_PREV(KL_G_ALLOC(nptr)));

    /* Remap old memory to new memory. */
    ret = CALL_SYS_REMAP(KL_G_PREV(KL_G_ALLOC(nptr)),\
      KL_G_PREV(KL_G_ALLOC(ptr)), osize, off);
    if (-1 == ret)
      return NULL;
  }
  else {
#endif
    /* Copy old memory to new memory. */
    memcpy(nptr, ptr, osize);
#ifdef CALL_SYS_REMAP
  }
#endif

  /* Release old memory region. */
  KL_free(ptr);

  if (size <= KL_brick_max_size())
    assert(KL_BRICK == KL_TYPEOF(KL_G_ALLOC(nptr)));
  else
    assert(KL_CHUNK == KL_TYPEOF(KL_G_ALLOC(nptr)));
  assert(0 != KL_G_SIZE(KL_G_ALLOC(nptr)));

  return nptr;
}


/****************************************************************************/
/* Release size bytes of memory */
/****************************************************************************/
KL_EXPORT int
KL_free(void * const ptr)
{
  kl_alloc_t * alloc;

  /* Enabled check. */
  GET_LOCK(&(mem.init_lock));
  if (M_ENABLED_ON != mem.enabled) {
    LET_LOCK(&(mem.init_lock));
    libc_free(ptr);
    return 0;
  }
  LET_LOCK(&(mem.init_lock));

  alloc = KL_G_ALLOC(ptr);

  switch (KL_TYPEOF(alloc)) {
    case KL_BRICK:
      kl_brick_put(&mem, (kl_brick_t*)alloc);
      break;
    case KL_CHUNK:
      kl_chunk_put(&mem, (kl_chunk_t*)alloc);
      break;
  }

  return 0;
}


/****************************************************************************/
/* Modify the KL environment */
/****************************************************************************/
KL_EXPORT int
KL_mallopt(int const param, int const value)
{
  if (param >= M_NUMBER)
    return 0;

  switch (param) {
    case M_ENABLED:
      switch (value) {
        case M_ENABLED_OFF:
          GET_LOCK(&(mem.init_lock));
          mem.enabled = M_ENABLED_OFF;
          LET_LOCK(&(mem.init_lock));
          kl_mem_destroy(&mem);
          break;
        case M_ENABLED_ON:
          kl_mem_init(&mem);
          GET_LOCK(&(mem.init_lock));
          mem.enabled = M_ENABLED_ON;
          LET_LOCK(&(mem.init_lock));
          break;
        case M_ENABLED_PAUSE:
          GET_LOCK(&(mem.init_lock));
          mem.enabled = M_ENABLED_PAUSE;
          LET_LOCK(&(mem.init_lock));
          break;
      }
      break;
  }

  return 1;
}


/****************************************************************************/
/* Return some memory statistics */
/****************************************************************************/
KL_EXPORT struct mallinfo
KL_mallinfo(void)
{
  struct mallinfo mi;

  mi.arena = mem.mem_max; /* maximum concurrent memory allocated */

  /* ----- UNIMPLEMENTED ----- */
  mi.smblks  = 0; /* total number of bricks */
  mi.ordblks = 0; /* total number of chunks */
  mi.hblks   = 0; /* total number of solo chunks (by definition, all in use) */

  mi.usmblks  = mem.mem_brick_cur;                   /* bytes used by bricks */
  mi.fsmblks  = mem.mem_brick_tot-mem.mem_brick_tot; /* bytes available for bricks */
  mi.uordblks = mem.mem_chunk_cur;                   /* bytes used by chunks */
  mi.fordblks = mem.mem_chunk_tot-mem.mem_chunk_tot; /* bytes available for chunks */
  /* ------------------------- */

  mi.hblkhd = mem.sys_ctr; /* calls to system allocator */

  mi.keepcost = mem.num_undes*BLOCK_DEFAULT_SIZE; /* bytes of undesignated blocks */

  return mi;
}
