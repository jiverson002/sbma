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
 *  1. Use fast bricks for allocations less than SB_CHUNK_MIN_SIZE.
 *  2. Don't automatically free large blocks.
 *  3. Allow coalescing of adjacent blocks.
 */

#if defined(USE_MMAP) && defined(USE_MEMALIGN)
# undef USE_MEMALIGN
#endif
#if defined(USE_MMAP) && defined(USE_SBMALLOC)
# undef USE_SBMALLOC
#endif
#if defined(USE_MEMALIGN) && defined(USE_SBMALLOC)
# undef USE_SBMALLOC
#endif
#if !defined(USE_MMAP) && !defined(USE_MEMALIGN) && !defined(USE_SBMALLOC)
# define USE_SBMALLOC
#endif

#ifdef USE_MMAP
# ifndef _BSD_SOURCE
#   define _BSD_SOURCE
#     include <sys/mman.h> /* mmap, munmap */
# endif
# undef _BSD_SOURCE
#endif
#ifdef USE_MEMALIGN
# ifndef _POSIX_C_SOURCE
#   define _POSIX_C_SOURCE 200112L
# elif _POSIX_C_SOURCE < 200112L
#   undef _POSIX_C_SOURCE
#   define _POSIX_C_SOURCE 200112L
# endif
# include <stdlib.h>  /* posix_memalign, size_t */
# undef _POSIX_C_SOURCE
#endif
#ifdef USE_SBMALLOC
# include "sbmalloc.h"
#endif

#include <assert.h>   /* assert */
#include <limits.h>   /* CHAR_BIT */
#include <stdint.h>   /* uint*_t */
#include <stdio.h>    /* printf */
#include <string.h>   /* memset */

/* This alignment should be a power of 2 >= sizeof(size_t). */
#ifdef MEMORY_ALLOCATION_ALIGNMENT
# define SB_MEMORY_ALLOCATION_ALIGNMENT MEMORY_ALLOCATION_ALIGNMENT
#else
# define SB_MEMORY_ALLOCATION_ALIGNMENT 8
#endif

#ifndef SB_EXPORT
# define SB_EXPORT extern
#endif


/****************************************************************************/
/* Compile time property checks. */
/****************************************************************************/
#define ct_assert_concat_(a, b) a##b
#define ct_assert_concat(a, b) ct_assert_concat_(a, b)
#define ct_assert(e) enum {ct_assert_concat(assert_line_,__LINE__)=1/(!!(e))}

/* Sanity check: void * and size_t are the same size, assumed in many places
 * in the code. */
ct_assert(sizeof(void*) == sizeof(size_t));
/* Sanity check: Alignment is >= sizeof(size_t). */
ct_assert(SB_MEMORY_ALLOCATION_ALIGNMENT >=sizeof(size_t));
/* Sanity check: SB_MEMORY_ALLOCATION_ALIGNMENT is a power of 2. */
ct_assert(0 == (SB_MEMORY_ALLOCATION_ALIGNMENT&(SB_MEMORY_ALLOCATION_ALIGNMENT-1)));


/****************************************************************************/
/* System memory allocation related macros */
/****************************************************************************/
#ifdef USE_MMAP
# define SB_SYS_ALLOC_FAIL      MAP_FAILED
# define SB_CALL_SYS_ALLOC(P,S) \
  ((P)=mmap(NULL, S, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0))
# define SB_CALL_SYS_FREE(P,S)  munmap(P,S)
# define SB_CALL_SYS_BZERO(P,S)
#endif
#ifdef USE_MEMALIGN
# define SB_SYS_ALLOC_FAIL      NULL
# define SB_CALL_SYS_ALLOC(P,S) \
  (0 == posix_memalign(&(P),SB_MEMORY_ALLOCATION_ALIGNMENT,S) ? (P) : NULL)
# define SB_CALL_SYS_FREE(P,S)  free(P)
# define SB_CALL_SYS_BZERO(P,S) memset(P, 0, S)
#endif
#ifdef USE_SBMALLOC
# define SB_SYS_ALLOC_FAIL      NULL
# define SB_CALL_SYS_ALLOC(P,S) ((P)=SB_sysalloc(S))
# define SB_CALL_SYS_FREE(P,S)  SB_sysfree(P)
# define SB_CALL_SYS_BZERO(P,S)
#endif


/****************************************************************************/
/* Relevant type shortcuts */
/****************************************************************************/
typedef uintptr_t uptr;


/****************************************************************************/
/* Accounting variables */
/****************************************************************************/
static size_t SB_SYS_CTR=0;
static size_t SB_MEM_TOTAL=0;
static size_t SB_MEM_MAX=0;


/****************************************************************************/
/* Block, brick, and chunk size constants */
/****************************************************************************/
#define SB_BLOCK_DEFAULT_SIZE (262144-SB_BLOCK_META_SIZE)
#define SB_BLOCK_HEADER_SIZE                                                \
(                                                                           \
  SB_MEMORY_ALLOCATION_ALIGNMENT == sizeof(size_t)                          \
    ? sizeof(size_t)                                                        \
    : SB_MEMORY_ALLOCATION_ALIGNMENT-sizeof(size_t)                         \
)
#define SB_BLOCK_FOOTER_SIZE  sizeof(size_t)
#define SB_BLOCK_META_SIZE    (SB_BLOCK_HEADER_SIZE+SB_BLOCK_FOOTER_SIZE)

#define SB_BRICK_HEADER_SIZE  sizeof(void *)
#define SB_BRICK_META_SIZE    SB_BRICK_HEADER_SIZ

#define SB_CHUNK_HEADER_SIZE  sizeof(size_t)
#define SB_CHUNK_FOOTER_SIZE  sizeof(size_t)
#define SB_CHUNK_META_SIZE    (SB_CHUNK_HEADER_SIZE+SB_CHUNK_FOOTER_SIZE)
#define SB_CHUNK_MIN_SIZE     SB_CHUNK_SIZE(2*sizeof(void*))


/****************************************************************************/
/*
 *  Memory block:
 *
 *    | size_t | `memory chunks' | size_t |
 *    +--------+-----------------+--------+
 *
 *    Memory blocks must be allocated so that their starting address is
 *    aligned to SB_MEMORY_ALLOCATION_ALIGNMENT, which is a power of 2.  For
 *    memory bricks, this is not necessarily required.  However, for memory
 *    chunks, this allows for the first and last memory chunks to be easily
 *    identifiable. In this case, these chunks are identified as the chunk
 *    which is preceded / succeeded by a size_t with the value
 *    SB_BLOCK_HEADER_SIZE, respectively.
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
 *    SB_MEMORY_ALLOCATION_ALIGNMENT.  Thus, if SB_MEMORY_ALLOCATION_ALIGNMENT
 *    > sizeof(size_t), the chunk address, indicated by ` above must be
 *    aligned to an address that is sizeof(size_t) less than a
 *    SB_MEMORY_ALLOCATION_ALIGNMENT aligned address.  To accomplish this for
 *    consecutive chunks, the size of each chunk is computed so that the
 *    following chunk will start at an address that is sizeof(size_t) less
 *    than a SB_MEMORY_ALLOCATION_ALIGNMENT aligned address.  Assuming that a
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
#define SB_ISALIGNED(V)                                                     \
  /* Sanity check: block is properly aligned. */                            \
  assert(0 == (((uptr)(V))&(SB_MEMORY_ALLOCATION_ALIGNMENT-1)))

static inline size_t SB_ALIGN(size_t const size)
{
  size_t const align = SB_MEMORY_ALLOCATION_ALIGNMENT;
  size_t const size_aligned = (size+(align-1))&(~(align-1));

  /* Sanity check: alignment is power of 2. */
  assert(0 == (align&(align-1)));
  /* Sanity check: size_aligned is properly aligned. */
  assert(0 == (size_aligned&(align-1)));

  return size_aligned;
}

static inline size_t SB_BLOCK_SIZE(size_t size)
{
  return SB_BLOCK_META_SIZE+size;
}

static inline size_t SB_CHUNK_SIZE(size_t size)
{
#ifdef WITH_BRICK
  return SB_ALIGN(SB_CHUNK_META_SIZE+size);
#else
  /* SB_CHUNK_MIN_SIZE is hardcoded here as
   * SB_ALIGN(SB_CHUNK_META_SIZE+2*sizeof(void*)) */

  size = SB_ALIGN(SB_CHUNK_META_SIZE+size);
  return size >= SB_ALIGN(SB_CHUNK_META_SIZE+2*sizeof(void*))
    ? size
    : SB_ALIGN(SB_CHUNK_META_SIZE+2*sizeof(void*));
#endif
}

static inline size_t SB_BRICK_SIZE(size_t size)
{
  return SB_ALIGN(SB_BRICK_HEADER_SIZE+size);
}

static inline size_t * SB_B2H(void * const block)
{
  SB_ISALIGNED(block);

  return (size_t*)((uptr)block+SB_BLOCK_HEADER_SIZE-sizeof(size_t));
}

static inline size_t * SB_B2F(void * const block, size_t const size)
{
  void * const footer = (void*)((uptr)block+size-SB_BLOCK_FOOTER_SIZE);

  SB_ISALIGNED(block);

  return (size_t*)footer;
}

static inline size_t SB_B2M(size_t * const block)
{
  SB_ISALIGNED(block);

  return (size_t)((*SB_B2H(block))&
          (~(((size_t)1<<((sizeof(size_t)-1)*CHAR_BIT))-1))) >>
          ((sizeof(size_t)-1)*CHAR_BIT);
}

static inline size_t SB_B2N(size_t * const block)
{
  SB_ISALIGNED(block);

  return (*SB_B2H(block))&(((size_t)1<<((sizeof(size_t)-1)*CHAR_BIT))-1);
}

static inline void * SB_B2R(void * const block)
{
  SB_ISALIGNED(block);

  return (void*)((uptr)block+SB_BLOCK_HEADER_SIZE);
}

static inline void * SB_B2C(void * const block)
{
  SB_ISALIGNED(block);

  return (void*)((uptr)block+SB_BLOCK_HEADER_SIZE);
}

static inline void ** SB_R2B(void * const brick)
{
  SB_ISALIGNED(brick);

  return (void**)brick;
}

static inline size_t * SB_R2H(void * const brick)
{
  SB_ISALIGNED(brick);

  return (size_t*)brick;
}

static inline void * SB_R2P(void * const brick)
{
  void * const ptr_aligned = (void*)((uptr)brick+SB_BRICK_HEADER_SIZE);

  SB_ISALIGNED(ptr_aligned);

  return ptr_aligned;
}

static inline void * SB_R2A(void * const brick)
{
  SB_ISALIGNED(SB_R2P(brick));

  /* Sanity check: valid multiplier. */
  assert(0 < SB_B2M(*SB_R2B(brick)) && SB_B2M(*SB_R2B(brick)) < 256);

  return (void*)((uptr)brick+
    SB_B2M(*SB_R2B(brick))*SB_MEMORY_ALLOCATION_ALIGNMENT);
}

static inline void * SB_C2P(void * const chunk)
{
  void * const ptr_aligned = (void*)((uptr)chunk+SB_CHUNK_HEADER_SIZE);

  SB_ISALIGNED(ptr_aligned);

  return ptr_aligned;
}

static inline size_t * SB_C2H(void * const chunk)
{
  SB_ISALIGNED(SB_C2P(chunk));

  return (size_t*)chunk;
}

static inline size_t * SB_C2F(void * const chunk)
{
  void * const footer = (void*)((uptr)chunk+*SB_C2H(chunk)-SB_CHUNK_FOOTER_SIZE);

  SB_ISALIGNED(SB_C2P(chunk));

  /* Sanity check: chunk header is not actually a block header. */
  assert(*SB_C2H(chunk) > SB_BLOCK_HEADER_SIZE);

  return (size_t*)footer;
}

static inline void * SB_C2E(void * const chunk)
{
  SB_ISALIGNED(SB_C2P(chunk));
  /* Sanity check: chunk size >= SB_BLOCK_HEADER_SIZE. */
  /* The reason that this is >= SB_BLOCK_HEADER_SIZE instead of strictly > is
   * to allow a valid chunk to traverse to the beginning of the block header
   * before it, when it is the first chunk. */
  assert(*SB_C2H(chunk) >= SB_BLOCK_HEADER_SIZE);

  return (void*)((uptr)chunk-(*(size_t*)((uptr)chunk-SB_CHUNK_FOOTER_SIZE)));
}

static inline void * SB_C2A(void * const chunk)
{
  SB_ISALIGNED(SB_C2P(chunk));
  /* Sanity check: chunk size > SB_BLOCK_HEADER_SIZE. */
  assert(*SB_C2H(chunk) > SB_BLOCK_HEADER_SIZE);

  return (void*)((uptr)chunk+(*SB_C2H(chunk)));
}

static inline void * SB_P2R(void * const ptr)
{
  SB_ISALIGNED(ptr);

  return (void*)((uptr)ptr-SB_BRICK_HEADER_SIZE);
}

static inline void * SB_P2C(void * const ptr)
{
  SB_ISALIGNED(ptr);

  return (void*)((uptr)ptr-SB_CHUNK_HEADER_SIZE);
}

static inline size_t * SB_P2H(void * const ptr)
{
  return SB_C2H(SB_P2C(ptr));
}

static inline void * SB_A2C(void * const after)
{
  SB_ISALIGNED(SB_C2P(after));
  /* Sanity check: after header is not actually a block footer. */
  assert(*SB_C2H(after) > SB_BLOCK_HEADER_SIZE);

  return (void*)((uptr)after-(*(size_t*)((uptr)after-SB_CHUNK_FOOTER_SIZE)));
}

static inline int SB_ISFIRST(void * const chunk)
{
  SB_ISALIGNED(SB_C2P(chunk));

  return SB_BLOCK_HEADER_SIZE == *(size_t*)((uptr)chunk-sizeof(size_t));
}

static inline int SB_ISLAST(void * const chunk)
{
  SB_ISALIGNED(SB_C2P(chunk));

  return SB_BLOCK_HEADER_SIZE == *(size_t*)SB_C2A(chunk);
}

static inline int SB_INUSE(void * const chunk)
{
  SB_ISALIGNED(SB_C2P(chunk));

  return 0 == *SB_C2F(chunk);
}


/****************************************************************************/
/* Base 2 integer logarithm */
/****************************************************************************/
#if !defined(__INTEL_COMPILER) && !defined(__GNUC__)
  static int sb_builtin_clzl(size_t v) {
    /* result will be nonsense if v is 0 */
    int i;
    for (i=sizeof(size_t)*CHAR_BIT-1; i>=0; --i) {
      if (v & ((size_t)1 << i))
        break;
    }
    return sizeof(size_t)*CHAR_BIT-i-1;
  }
  #define sb_clz(V) sb_builtin_clzl(V)
#else
  #define sb_clz(V) __builtin_clzl(V)
#endif

#define SBLOG2(V) (sizeof(size_t)*CHAR_BIT-1-sb_clz(V))



/****************************************************************************/
/* Lookup tables to convert between size and brick_bin number */
/****************************************************************************/
#define SBNUMBRICKBIN 256

#define SBBRICKBIN2SIZE(B) \
  (assert((B)<SBNUMBRICKBIN), ((B)+1)*SB_MEMORY_ALLOCATION_ALIGNMENT)
#define SBSIZE2BRICKBIN(S) ((S)/SB_MEMORY_ALLOCATION_ALIGNMENT-1)


/****************************************************************************/
/* Lookup tables to convert between size and chunk_bin number */
/****************************************************************************/
#define SBNUMCHUNKBIN   1576
#define SBSMALLCHUNKBIN 1532

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

static size_t bin2size[SBSMALLCHUNKBIN]=
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

#define SBISSMALLBIN(B) (B < SBSMALLCHUNKBIN)

#define SBSQR(V) ((V)*(V))

#define SBSIZE2BIN(S)                                                       \
  (                                                                         \
    assert(0 != (S)),                                                       \
    ((S)<=64)                                                               \
      ? ((S)-1)/8                                                           \
      : (SBLOG2((S)-1)<20)                                                  \
        ? ((S)>=SBSQR(log2size[SBLOG2((S)-1)-1])+1)                         \
          ? log2off[SBLOG2((S)-1)] +                                        \
            ((S)-(SBSQR(log2size[SBLOG2((S)-1)-1])+1)) /                    \
            log2size[SBLOG2((S)-1)]                                         \
          : log2off[SBLOG2((S)-1)-1] +                                      \
            ((S)-(SBSQR(log2size[SBLOG2((S)-1)-2])+1)) /                    \
            log2size[SBLOG2((S)-1)-1]                                       \
        : log2off[SBLOG2((S)-1)]                                            \
  )

#define SBBIN2SIZE(B) (assert((B)<SBSMALLCHUNKBIN), bin2size[(B)])


/****************************************************************************/
/****************************************************************************/
/* Free chunk data structure API */
/****************************************************************************/
/****************************************************************************/

/****************************************************************************/
/* Free chunk data structure node */
/****************************************************************************/
typedef struct sb_brick_bin_node
{
  struct sb_brick_bin_node * n; /* next node */
} sb_brick_bin_node_t;


/****************************************************************************/
/* Free chunk data structure node */
/****************************************************************************/
typedef struct sb_chunk_bin_node
{
  struct sb_chunk_bin_node * p; /* previous node */
  struct sb_chunk_bin_node * n; /* next node */
} sb_chunk_bin_node_t;


/****************************************************************************/
/* Free chunk data structure */
/****************************************************************************/
typedef struct sb_mem
{
  int init;
  void * last_block;
  struct sb_brick_bin_node * brick_bin[SBNUMBRICKBIN];
  struct sb_chunk_bin_node * chunk_bin[SBNUMCHUNKBIN];
} sb_mem_t;


/****************************************************************************/
/* Initialize free chunk data structure */
/****************************************************************************/
static int
sb_mem_init(sb_mem_t * const mem)
{
  int i;

  for (i=0; i<SBNUMBRICKBIN; ++i)
    mem->brick_bin[i] = NULL;

  for (i=0; i<SBNUMCHUNKBIN; ++i)
    mem->chunk_bin[i] = NULL;

  mem->init = 1;

  return 0;
}


#ifdef WITH_BRICK
/****************************************************************************/
/* Add a node to a free brick data structure */
/****************************************************************************/
static int
sb_brick_bin_ad(sb_mem_t * const mem, void * const brick)
{
  size_t bidx = SBSIZE2BRICKBIN(*SB_R2H(brick));
  sb_brick_bin_node_t * node = (sb_brick_bin_node_t*)SB_R2P(brick);

  /* Sanity check: node is not the head of brick_bin[bidx]. */
  assert(node != mem->brick_bin[bidx]);
  /* Sanity check: node has no dangling pointers. */
  assert(NULL == node->n);
  /* Sanity check: node is not in use. */
  assert(SBBRICKBIN2SIZE(bidx) == *SB_R2H(brick));

  /* Prepend node to front of brick_bin[bidx] linked-list. */
  node->n = mem->brick_bin[bidx];
  mem->brick_bin[bidx] = node;

  return 0;
}


/****************************************************************************/
/* Find the brick bin with the smallest size >= size parameter */
/****************************************************************************/
static void *
sb_brick_bin_find(sb_mem_t * const mem, size_t const size)
{
  size_t bidx = SBSIZE2BRICKBIN(size);
  void * brick = NULL;
  sb_brick_bin_node_t * n;

  /* Get head of brick_bin[bidx]. */
  n = mem->brick_bin[bidx];

  /* Remove head of brick_bin[bidx]. */
  if (NULL != n) {
    brick = SB_P2R(n);

    /* Sanity check: block must not be empty. */
    assert(0 != SB_B2N(*SB_R2B(brick)));

    /* Decrement block count.  This is safe because the count occupies the low
     * bytes of the header. */
    (*SB_B2H(*SB_R2B(brick)))--;

    /* Check if brick has never been previously used. */
    if (NULL == n->n) {
      *SB_R2B(brick) = mem->last_block;

      /* Sanity check: next pointer must be NULL. */
      assert(NULL == n->n);

      /* Set next pointer when block is not empty. */
      if (0 != SB_B2N(*SB_R2B(brick)))
        n->n = SB_R2P(SB_R2A(brick));
    }

    /* Set head of brick_bin[bidx]. */
    mem->brick_bin[bidx] = n->n;

    /* Set the block for n->n brick, so in a subsequent sb_brick_bin_find,
     * SB_R2B can be used on said brick. */
    if (NULL != n->n)
      *SB_R2B(SB_P2R(n->n)) = *SB_R2B(brick);

    n->n = NULL;
  }

  return brick;
}
#endif


/****************************************************************************/
/* Add a node to a free chunk data structure */
/****************************************************************************/
static int
sb_chunk_bin_ad(sb_mem_t * const mem, void * const chunk)
{
  size_t bidx = SBSIZE2BIN(*SB_C2H(chunk));
  sb_chunk_bin_node_t * p, * n, * node = (sb_chunk_bin_node_t*)SB_C2P(chunk);

  /* Treat fixed size bins and large bins differently */
  if (SBISSMALLBIN(bidx)) {
    /* Sanity check: chunk_bin[bidx] is empty or the previous pointer of its
     * head node is NULL. */
    assert(NULL == mem->chunk_bin[bidx] || NULL == mem->chunk_bin[bidx]->p);
    /* Sanity check: node is not the head of chunk_bin[bidx]. */
    assert(node != mem->chunk_bin[bidx]);
    /* Sanity check: node has no dangling pointers. */
    assert(NULL == node->p && NULL == node->n);
    /* Sanity check: node is not in use. */
    assert(*SB_C2H(chunk) == *SB_C2F(chunk));

    /* Shift bin index in case a chunk is made up of coalesced chunks which
     * collectively have a size which causes the chunk to fall in a particular
     * bin, but has less bytes than required by the bin.
     * TODO: it should be possible to do this with an if statement, since it
     * should shift down by one bin at most. */
    while (*SB_C2H(chunk) < SBBIN2SIZE(bidx) && bidx > 0)
      bidx--;

    /* Sanity check: chunk must be at least the size of the fixed bin. */
    assert(*SB_C2H(chunk) >= SBBIN2SIZE(bidx));

    /* Prepend n to front of chunk_bin[bidx] linked-list. */
    node->p = NULL;
    node->n = mem->chunk_bin[bidx];
    if (NULL != mem->chunk_bin[bidx]) {
      assert(NULL == mem->chunk_bin[bidx]->p);
      mem->chunk_bin[bidx]->p = node;
    }
    mem->chunk_bin[bidx] = node;
  }
  else {
    /* This will keep large buckets sorted. */
    n = mem->chunk_bin[bidx];
    p = NULL;

    while (NULL != n && *SB_P2H(n) < *SB_C2H(chunk)) {
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

  return 0;
}


/****************************************************************************/
/* Remove a node from a free chunk data structure */
/****************************************************************************/
static int
sb_chunk_bin_rm(sb_mem_t * const mem, void * const chunk)
{
  size_t bidx = SBSIZE2BIN(*SB_C2H(chunk));
  sb_chunk_bin_node_t * node = (sb_chunk_bin_node_t*)SB_C2P(chunk);

  /* Shift bin index in case a chunk is made up of coalesced chunks which
   * collectively have a size which causes the chunk to fall in a particular
   * bin, but has less bytes than required by the bin.
   * TODO: it should be possible to do this with an if statement, since it
   * should shift down by one bin at most. */
  while (*SB_C2H(chunk) < SBBIN2SIZE(bidx) && bidx > 0)
    bidx--;

  /* Sanity check: node has correct pointer structure. */
  assert(NULL != node->p || NULL != node->n || mem->chunk_bin[bidx] == node);

  /* Fixed and variable sized bins are treated the same, since removing a node
   * from a variable sized bin will not cause it to become unsorted. */
  if (NULL == node->p)
    mem->chunk_bin[bidx] = node->n;
  else
    node->p->n = node->n;
  if (NULL != node->n)
    node->n->p = node->p;
  node->n = NULL;
  node->p = NULL;

  /* Sanity check: chunk_bin[bidx] is empty or the previous pointer of its
   * head node is NULL. */
  assert(NULL == mem->chunk_bin[bidx] || NULL == mem->chunk_bin[bidx]->p);

  return 0;
}


/****************************************************************************/
/* Find the chunk bin with the smallest size >= size parameter */
/****************************************************************************/
static void *
sb_chunk_bin_find(sb_mem_t * const mem, size_t const size)
{
  size_t bidx = SBSIZE2BIN(size);
  sb_chunk_bin_node_t * n;

  if (SBISSMALLBIN(bidx)) {
    /* Find first chunk bin with a node. */
    n = mem->chunk_bin[bidx];
    while (NULL == n && SBISSMALLBIN(bidx))
      n = mem->chunk_bin[++bidx];

    /* Remove head of chunk_bin[bidx]. */
    if (NULL != n) {
      assert(NULL != mem->chunk_bin[bidx]);
      assert(NULL == n->p);
      mem->chunk_bin[bidx] = n->n;
      if (NULL != n->n) {
        n->n->p = NULL;
        n->n = NULL;
      }

      /* Sanity check: chunk must be large enough to hold request. */
      assert(size <= *SB_P2H(n));
    }

    assert(NULL == mem->chunk_bin[bidx] || NULL == mem->chunk_bin[bidx]->p);
  }
  else {
    /* Find first chunk_bin with a node. */
    n = mem->chunk_bin[bidx];
    while (NULL == n && bidx < SBNUMCHUNKBIN-1)
      n = mem->chunk_bin[++bidx];

    assert(NULL == n);

    /* Find first node in chunk_bin[bidx] with size >= size parameter. */
    while (NULL != n && *SB_P2H(n) < size)
      n = n->n;

    /* Remove n from chunk_bin[bidx]. */
    if (NULL != n) {
      if (NULL == n->p) {
        mem->chunk_bin[bidx] = n->n;
        if (NULL != n->n)
          n->n->p = NULL;
      }
      else {
        n->p->n = n->n;
        if (NULL != n->n)
          n->n->p = n->p;
      }
      n->p = NULL;
      n->n = NULL;
    }
  }

  return (NULL == n) ? NULL : SB_P2C(n);
}


/****************************************************************************/
/****************************************************************************/
/* SB API */
/****************************************************************************/
/****************************************************************************/

/****************************************************************************/
/* Free brick/chunk data structure */
/****************************************************************************/
static sb_mem_t mem={.init=0};


/****************************************************************************/
/* Initialize static variables and data structures */
/****************************************************************************/
#define SB_INIT_CHECK                                                       \
do {                                                                        \
  if (0 == mem.init)                                                        \
    sb_mem_init(&mem);                                                      \
} while (0)


/****************************************************************************/
/* Allocate size bytes of memory */
/****************************************************************************/
SB_EXPORT void *
SB_malloc(size_t const size)
{
  size_t block_size, chunk_size, mult;
  void * ret, * block, * brick, * chunk, * ptr;

  /*
      Basic algorithm:
        If a small request (< SB_CHUNK_MIN_SIZE bytes):
          1. If one exists, use a fixed size memory brick
          2. If available, create a new block of memory bricks, and use the
             first
        If small request (< SB_BLOCK_DEFAULT_SIZE - per-block overhead):
          1. If one exists, use a fixed size memory chunk.
          2. If available, create a new block and split it to obtain an
             appropriate sized memory chunk
        Otherwise, for a large request:
          1. If available, get required memory plus per-block overhead bytes
             from system and use it
  */

  SB_INIT_CHECK;

#ifdef WITH_BRICK
  if (SB_CHUNK_SIZE(size) < SB_CHUNK_MIN_SIZE) {
    if (NULL == (brick=sb_brick_bin_find(&mem, SB_BRICK_SIZE(size)))) {
      block_size = SB_BLOCK_SIZE(SB_BLOCK_DEFAULT_SIZE);

      /* Accounting. */
      SB_MEM_TOTAL += block_size;
      if (SB_MEM_TOTAL > SB_MEM_MAX)
        SB_MEM_MAX = SB_MEM_TOTAL;

      /* Get system memory. */
      ret = SB_CALL_SYS_ALLOC(block, block_size);
      if (SB_SYS_ALLOC_FAIL == ret)
        return NULL;
      SB_SYS_CTR++;

      /* Zero memory. */
      SB_CALL_SYS_BZERO(block, block_size);

      /* Set block multiplier and zero out block count. */
      mult = SB_BRICK_SIZE(size)/SB_MEMORY_ALLOCATION_ALIGNMENT;
      /* Sanity check: mult must be at least 1. */
      assert(mult >= 1);

      /* Set B2M(block) and B2N(block). */
      *SB_B2H(block) = mult<<((sizeof(size_t)-1)*CHAR_BIT);
      *SB_B2H(block) |= ((SB_BLOCK_DEFAULT_SIZE+SB_BLOCK_FOOTER_SIZE)/
          (mult*SB_MEMORY_ALLOCATION_ALIGNMENT)-1);

      /* Setup brick. */
      brick = SB_B2R(block);
      *SB_R2B(brick) = block;

      if (0 < *SB_B2H(block)) {
        /* Hard code SB_R2A, since the next brick has no assigned block. */
        mem.brick_bin[SBSIZE2BRICKBIN(SB_BRICK_SIZE(size))] =
          (void*)((uptr)brick+mult*SB_MEMORY_ALLOCATION_ALIGNMENT);

        /* Set the block for the head of the bin. */
        *SB_R2B(SB_P2R(mem.brick_bin[SBSIZE2BRICKBIN(SB_BRICK_SIZE(size))])) =
          block;
      }

      ptr = SB_R2P(brick);

      mem.last_block = block;
    }
  }
  else {
#endif
    /* Sanity check: allocation size is valid. */
    assert(SB_CHUNK_SIZE(size) >= SB_CHUNK_MIN_SIZE);

    /* Check for available memory chunk. */
    if (NULL == (chunk=sb_chunk_bin_find(&mem, SB_CHUNK_SIZE(size)))) {
      /* Determine appropriate allocation size. */
      if (SB_CHUNK_SIZE(size) <= SB_BLOCK_DEFAULT_SIZE)
        block_size = SB_BLOCK_SIZE(SB_BLOCK_DEFAULT_SIZE);
      else
        block_size = SB_BLOCK_SIZE(SB_CHUNK_SIZE(size));

      /* Accounting. */
      SB_MEM_TOTAL += block_size;
      if (SB_MEM_TOTAL > SB_MEM_MAX)
        SB_MEM_MAX = SB_MEM_TOTAL;

      /* Get system memory. */
      ret = SB_CALL_SYS_ALLOC(block, block_size);
      if (SB_SYS_ALLOC_FAIL == ret)
        return NULL;
      SB_SYS_CTR++;

      /* Zero memory. */
      SB_CALL_SYS_BZERO(block, block_size);

      /* Set block header and footer size. */
      *SB_B2H(block) = SB_BLOCK_HEADER_SIZE;
      *SB_B2F(block, block_size) = *SB_B2H(block);

      /* Set the chunk to be returned. */
      chunk = SB_B2C(block);

      /* Set chunk header and footer size (set chunk as not in use). */
      *SB_C2H(chunk) = block_size-SB_BLOCK_META_SIZE;
      *SB_C2F(chunk) = *SB_C2H(chunk);

      /* Sanity check: macros are working. */
      assert(SB_ISFIRST(chunk));
      assert(SB_ISLAST(chunk));
      assert(!SB_INUSE(chunk));
    }

    /* Split chunk if applicable. */
    if (*SB_C2H(chunk) > SB_CHUNK_MIN_SIZE &&
        SB_CHUNK_SIZE(size) <= *SB_C2H(chunk)-SB_CHUNK_MIN_SIZE)
    {
      /* Get current chunk[0] size. */
      chunk_size = *SB_C2H(chunk);

      /* Update chunk[0] size. */
      *SB_C2H(chunk) = SB_CHUNK_SIZE(size);

      /* Set header and footer chunk[1] size (not in use). */
      *SB_C2H(SB_C2A(chunk)) = chunk_size-*SB_C2H(chunk);
      *SB_C2F(SB_C2A(chunk)) = *SB_C2H(SB_C2A(chunk));

      /* Add chunk[1] to free chunk data structure.  In case NDEBUG is not
       * defined, the pointers should be reset.  This is necessary whenever
       * memory chunks have been coallesced, since the memory locations that the
       * pointers occupy may have been modified if one of the coalesced chunks
       * was used as an allocation. */
  #ifndef NDEBUG
      memset(SB_C2P(SB_C2A(chunk)), 0, sizeof(sb_chunk_bin_node_t));
  #endif
      if (0 != sb_chunk_bin_ad(&mem, SB_C2A(chunk)))
        return NULL;

      /* Sanity check: chunk[0] can be reached from chunk[1] */
      assert((*SB_C2F(chunk) = *SB_C2H(chunk), chunk == SB_A2C(SB_C2A(chunk))));
    }

    /* Set chunk[0] as in use */
    *SB_C2F(chunk) = 0;

    /* Get pointer to return. */
    ptr = SB_C2P(chunk);

    /* Sanity check: chunk size is still valid. */
    assert(*SB_C2H(chunk) >= SB_CHUNK_MIN_SIZE);
    /* Sanity check: macros are working. */
    assert(chunk == SB_P2C(ptr));
    /* Sanity check: pointer points to a valid piece of memory. */
    assert((uptr)ptr+size <= (uptr)chunk+*SB_C2H(chunk)-SB_CHUNK_FOOTER_SIZE);
#ifdef WITH_BRICK
  }
#endif

  /* Sanity check: returned pointer is properly aligned. */
  assert(0 == ((uptr)ptr&(SB_MEMORY_ALLOCATION_ALIGNMENT-1)));
  return ptr;
}


/****************************************************************************/
/* Allocate num*size bytes of zeroed memory */
/****************************************************************************/
SB_EXPORT void *
SB_calloc(size_t const num, size_t const size)
{
  SB_INIT_CHECK;

  void * ptr = SB_malloc(num*size);
  if (NULL != ptr)
    memset(ptr, 0, num*size);
  return ptr;
}


/****************************************************************************/
/* Reallocate size bytes at address ptr, if possible. */
/****************************************************************************/
SB_EXPORT void *
SB_realloc(void * const ptr, size_t const size)
{
  void * nptr;

  SB_INIT_CHECK;

  if (NULL == ptr)
    return SB_malloc(size);

  if (*SB_P2H(ptr) >= SB_CHUNK_SIZE(size))
    return ptr;

  if (NULL == (nptr=SB_malloc(size)))
    return NULL;

  memcpy(nptr, ptr, size);

  SB_free(ptr);

  return nptr;
}


/****************************************************************************/
/* Release size bytes of memory */
/****************************************************************************/
SB_EXPORT void
SB_free(void * const ptr)
{
  void * chunk = SB_P2C(ptr);

  SB_INIT_CHECK;

  /* TODO: how to distinguish between fast bricks and chunks when given a
   * pointer. */

  /* Coalesce with previous chunk. */
  if (!SB_ISFIRST(chunk) && !SB_INUSE(SB_A2C(chunk))) {
    /* Remove previous chunk from free chunk data structure. */
    if (0 != sb_chunk_bin_rm(&mem, SB_A2C(chunk)))
      return;

    /* Set chunk to point to previous chunk. */
    chunk = SB_A2C(chunk);

    /* Update chunk size. */
    *SB_C2H(chunk) = *SB_C2H(chunk)+*SB_C2H(SB_C2A(chunk));
  }

  /* Coalesce with following chunk. */
  if (!SB_ISLAST(chunk) && !SB_INUSE(SB_C2A(chunk))) {
    /* Remove following chunk from free chunk data structure. */
    if (0 != sb_chunk_bin_rm(&mem, SB_C2A(chunk)))
      return;

    /* Update chunk size. */
    *SB_C2H(chunk) = *SB_C2H(chunk)+*SB_C2H(SB_C2A(chunk));
  }

  /* Sanity check: chunk size is still valid. */
  assert(*SB_C2H(chunk) >= SB_CHUNK_MIN_SIZE);

  /* TODO: Implicitly, the following rule prevents large allocations from
   * going into the free chunk data structure.  Thus, it also prevents the
   * limitation described above.  However, in many cases, it would be nice to
   * keep large allocations around for quicker allocation time. */

  /* If chunk is the only chunk, release memory back to system. */
  if (SB_ISFIRST(chunk) && SB_ISLAST(chunk)) {
    /* Accounting. */
    SB_MEM_TOTAL -= SB_BLOCK_SIZE(*SB_C2H(chunk));

    SB_CALL_SYS_FREE(SB_C2E(chunk), SB_BLOCK_SIZE(*SB_C2H(chunk)));
  }
  else {
    /* Set chunk as not in use. */
    *SB_C2F(chunk) = *SB_C2H(chunk);

//#ifndef NDEBUG
//    memset(SB_C2P(chunk), 0, sizeof(sb_chunk_bin_node_t));
//#endif
    /* Add chunk to free chunk data structure. */
    if (0 != sb_chunk_bin_ad(&mem, chunk))
      return;
  }
}


/****************************************************************************/
/* Print some memory statistics */
/****************************************************************************/
SB_EXPORT void
SB_malloc_stats(void)
{
  printf("Calls to system allocator = %zu\n", SB_SYS_CTR);
  printf("Maximum concurrent memory = %zu\n", SB_MEM_MAX);
  fflush(stdout);
}
