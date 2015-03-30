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
 *  1. Use fast bricks for allocations less than CHUNK_MIN_SIZE.
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
#include <unistd.h>

#include "klmalloc.h" /* klmalloc library */

/* This alignment should be a power of 2 >= sizeof(size_t). */
#ifndef MEMORY_ALLOCATION_ALIGNMENT
# define MEMORY_ALLOCATION_ALIGNMENT 8
#endif

#ifndef KL_EXPORT
# define KL_EXPORT extern
#endif


/****************************************************************************/
/* Memory data structures. */
/****************************************************************************/
#define BLOCK_DEFAULT_SIZE 262144

#define BLOCK_HEADER_ALIGN                                                  \
(                                                                           \
  MEMORY_ALLOCATION_ALIGNMENT <= 2*sizeof(uintptr_t)                        \
    ? MEMORY_ALLOCATION_ALIGNMENT                                           \
    : MEMORY_ALLOCATION_ALIGNMENT-2*sizeof(uintptr_t)                       \
)

#define FIXED_MAX_SIZE                                                      \
  (BLOCK_DEFAULT_SIZE-sizeof(uintptr_t)-BLOCK_HEADER_ALIGN-                 \
  sizeof(kl_brick_t*)-2*sizeof(struct kl_fix_block*))

typedef struct kl_alloc
{
  uintptr_t info;
  char * raw;
} kl_alloc_t;

struct kl_brick;

typedef struct kl_brick_node
{
  struct kl_brick * next;
} kl_brick_node_t;

typedef struct kl_brick
{
  uintptr_t info;
  union {
    kl_brick_node_t node;
    char * raw;
  } iface;
} kl_brick_t;

struct kl_chunk;

typedef struct kl_chunk_node
{
  struct kl_chunk * prev;
  struct kl_chunk * next;
} kl_chunk_node_t;

typedef struct kl_chunk
{
  uintptr_t info;
  union {
    kl_chunk_node_t node;
    char * raw;
  } iface;
} kl_chunk_t;

typedef struct kl_fix_block
{
  uintptr_t info;
  char _pad[BLOCK_HEADER_ALIGN];
  char raw[FIXED_MAX_SIZE];
  kl_brick_t * head;
  struct kl_fix_block * prev;
  struct kl_fix_block * next;
} kl_fix_block_t;

typedef struct kl_var_block
{
  uintptr_t info;
  char _pad[BLOCK_HEADER_ALIGN];
  char raw[];
} kl_var_block_t;


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
/* System memory allocation related macros */
/****************************************************************************/
#ifdef USE_MMAP
# define SYS_ALLOC_FAIL      MAP_FAILED
# define CALL_SYS_ALLOC(P,S) \
  ((P)=mmap(NULL, S, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0))
# define CALL_SYS_FREE(P,S)  munmap(P,S)
# define CALL_SYS_BZERO(P,S)
#endif
#ifdef USE_MEMALIGN
# define SYS_ALLOC_FAIL      NULL
# define CALL_SYS_ALLOC(P,S) \
  (0 == posix_memalign(&(P),MEMORY_ALLOCATION_ALIGNMENT,S) ? (P) : NULL)
# define CALL_SYS_FREE(P,S)  free(P)
# define CALL_SYS_BZERO(P,S) memset(P, 0, S)
#endif
#ifdef USE_SBMALLOC
# define SYS_ALLOC_FAIL      NULL
# define CALL_SYS_ALLOC(P,S) ((P)=SB_malloc(S))
# define CALL_SYS_FREE(P,S)  SB_free(P)
# define CALL_SYS_BZERO(P,S)
#endif


/****************************************************************************/
/* Accounting variables */
/****************************************************************************/
static size_t KL_SYS_CTR=0;
static size_t KL_MEM_TOTAL=0;
static size_t KL_MEM_MAX=0;


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

#define BRICK_MAX_SIZE   (2*sizeof(void*)-1)

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

static inline size_t KL_BRICK_SIZE(size_t size)
{
  return KL_ALIGN(sizeof(uintptr_t)+size);
}

static inline int KL_TYPEOF(kl_alloc_t const * const alloc)
{
  assert(KL_ISALIGNED(&alloc->raw));
  return alloc->info&KL_BRICK;
}

static inline int KL_G_BRICKBIN(size_t const size)
{
  return KL_BRICK_SIZE(size)/MEMORY_ALLOCATION_ALIGNMENT-1;
}

static inline size_t KL_G_COUNT(kl_fix_block_t const * const block)
{
  assert(KL_ISALIGNED((void*)block));
  return (size_t)(block->info&BLOCK_COUNT_MASK);
}

static inline size_t KL_G_BIDX(kl_fix_block_t const * const block)
{
  assert(KL_ISALIGNED((void*)block));
  return (size_t)((block->info&BLOCK_BIDX_MASK)>>BLOCK_BIDX_SHIFT);
}

static inline void * KL_G_BLOCK(kl_alloc_t const * const alloc)
{
  void * ptr=NULL;
  kl_brick_t * brick;

  assert(KL_ISALIGNED(&alloc->raw));

  switch (KL_TYPEOF(alloc)) {
    case KL_BRICK:
      brick = (kl_brick_t*)alloc;
      ptr = (void*)(brick->info&(~((uintptr_t)KL_BRICK)));
      break;
    case KL_CHUNK:
      //ptr = (void*)((uptr)hdr-(*(size_t const*)((uptr)hdr-KL_CHUNK_FOOTER_SIZE)));
      break;
  }

  assert(KL_ISALIGNED(ptr));

  return ptr;
}

static inline size_t KL_G_SIZE(kl_alloc_t const * const alloc)
{
  kl_fix_block_t * block;
  kl_brick_t * brick;
  kl_chunk_t * chunk;

  assert(KL_ISALIGNED(&alloc->raw));

  switch (KL_TYPEOF(alloc)) {
    case KL_BRICK:
      brick = (kl_brick_t*)alloc;
      block = (kl_fix_block_t*)KL_G_BLOCK((kl_alloc_t*)brick);
      return (KL_G_BIDX(block)+1)*MEMORY_ALLOCATION_ALIGNMENT;
    case KL_CHUNK:
      chunk = (kl_chunk_t*)alloc;
      return (size_t)chunk->info;
  }
  return 0;
}

static inline kl_alloc_t * KL_G_ALLOC(void const * const ptr)
{
  assert(KL_ISALIGNED(ptr));
  return (kl_alloc_t*)((uintptr_t)ptr-sizeof(uintptr_t));
}

static inline kl_alloc_t * KL_G_NEXT(kl_alloc_t const * const alloc)
{
  assert(KL_ISALIGNED(&alloc->raw));
  return (kl_alloc_t*)((uintptr_t)alloc+KL_G_SIZE(alloc));
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


/****************************************************************************/
/* Free memory data structure */
/****************************************************************************/
#define BRICK_BIN_NUM 256
#define CHUNK_BIN_NUM 1576

typedef struct kl_mem
{
  int init;
  void * last_block;
  kl_fix_block_t * brick_bin[BRICK_BIN_NUM];
  kl_var_block_t * chunk_bin[CHUNK_BIN_NUM];
} kl_mem_t;


/****************************************************************************/
/* Initialize free chunk data structure */
/****************************************************************************/
static int
kl_mem_init(kl_mem_t * const mem)
{
  int i;

  for (i=0; i<BRICK_BIN_NUM; ++i)
    mem->brick_bin[i] = NULL;

  for (i=0; i<CHUNK_BIN_NUM; ++i)
    mem->chunk_bin[i] = NULL;

  mem->init = 1;

  return 0;
}


/****************************************************************************/
/* Allocate a new block */
/****************************************************************************/
static kl_fix_block_t *
kl_fix_block_alloc(void)
{
  void * ret, * block;

  /* Get system memory. */
  ret = CALL_SYS_ALLOC(block, sizeof(kl_fix_block_t));
  if (SYS_ALLOC_FAIL == ret)
    return NULL;
  CALL_SYS_BZERO(block, sizeof(kl_fix_block_t));

  /* Accounting. */
  KL_MEM_TOTAL += sizeof(kl_fix_block_t);
  if (KL_MEM_TOTAL > KL_MEM_MAX)
    KL_MEM_MAX = KL_MEM_TOTAL;
  KL_SYS_CTR++;

  return block;
}


/****************************************************************************/
/* Add a node to a free brick data structure */
/****************************************************************************/
static int
kl_brick_bin_ad(kl_mem_t * const mem, kl_brick_t * const brick)
{
  size_t bidx;
  kl_fix_block_t * block;

  /* Sanity check: brick is not NULL. */
  assert(NULL != brick);

  block = (kl_fix_block_t*)KL_G_BLOCK((kl_alloc_t*)brick);
  bidx  = KL_G_BIDX(block);

  /* Sanity check: brick is not the head of brick_bin[bidx]. */
  assert(brick != block->head);
  /* Sanity check: node has no dangling pointers. */
  assert(NULL == brick->iface.node.next);

  /* Increment block count.  This is safe (won't affect multiplier) because
   * the count occupies the low bytes of the header. */
  block->info++;

  if (KL_ISEMPTY(block)) {
    /*if ('undesignated' not full) {
    }
    else {*/
      /* Remove from doubly-linked list of blocks in bin[bidx]. */
      if (NULL != block->prev)
        block->prev->next = block->next;
      else
        mem->brick_bin[bidx] = block->next;
      if (NULL != block->next)
        block->next->prev = block->prev;

      /* Release back to system. */
      CALL_SYS_FREE(block, BLOCK_DEFAULT_SIZE);

      /* Accounting. */
      KL_MEM_TOTAL -= BLOCK_DEFAULT_SIZE;
    /*}*/
  }
  else {
    /* Prepend brick to front of block singly-linked list. */
    brick->iface.node.next = block->head;
    block->head = brick;

    /* If not already part of doubly-linked list, add block to it. */
    if (NULL == block->prev && NULL == block->next &&
        block != mem->brick_bin[bidx])
    {
      if (NULL != mem->brick_bin[bidx])
        mem->brick_bin[bidx]->prev = block;
      block->next = mem->brick_bin[bidx];
      mem->brick_bin[bidx] = block;
    }
  }

  return 0;
}


/****************************************************************************/
/* Find the brick bin with the smallest size >= size parameter */
/****************************************************************************/
static void *
kl_brick_bin_find(kl_mem_t * const mem, size_t const size)
{
  size_t bidx;
  kl_fix_block_t * block;
  kl_brick_t * brick;

  if (size > BRICK_MAX_SIZE)
    return NULL;

  bidx  = KL_G_BRICKBIN(size);
  block = mem->brick_bin[bidx];

#if 0
  if (NULL != 'undesignated block') { /* Designate existing block. */
  }
#endif
  if (NULL == block) {                /* Allocate new block. */
    block = kl_fix_block_alloc();
    if (NULL == block)
      return NULL;

    /* Set block bin index (multiplier-1). */
    block->info = bidx<<BLOCK_BIDX_SHIFT;
    /* Set block count. */
    block->info |= (FIXED_MAX_SIZE/((bidx+1)*MEMORY_ALLOCATION_ALIGNMENT));

    /* Set block as head of doubly-linked list. */
    if (NULL != mem->brick_bin[bidx])
      mem->brick_bin[bidx]->prev = block;
    block->next = mem->brick_bin[bidx];
    mem->brick_bin[bidx] = block;

    /* Set head of blocks singly-linked list. */
    block->head = (kl_brick_t*)&block->raw;
  }
  else {                              /* Use head block. */
  }

  /* Decrement block count.  This is safe (won't affect multiplier) because
   * the count occupies the low bytes of the header. */
  block->info--;

  /* Remove full block from doubly-linked list. */
  if (KL_ISFULL(block)) {
    if (NULL != block->prev)
      block->prev->next = block->next;
    else
      mem->brick_bin[bidx] = block->next;
    if (NULL != block->next)
      block->next->prev = block->prev;
  }

  /* Get brick. */
  brick = block->head;

  /* Check if brick has never been previously used. */
  if (0 == brick->info) {
    /* Set block pointer. */
    brick->info = (uintptr_t)block|KL_BRICK;

    /* Check if block is not full. */
    if (!KL_ISFULL(block)) {
      /* Set next pointer. */
      brick->iface.node.next = (kl_brick_t*)KL_G_NEXT((kl_alloc_t*)brick);
    }
  }

  /* Set new head of block. */
  block->head = brick->iface.node.next;

  /* Clear pointer. */
  brick->iface.node.next = NULL;

  return brick;
}


/****************************************************************************/
/****************************************************************************/
/* KL API */
/****************************************************************************/
/****************************************************************************/

/****************************************************************************/
/* Free memory data structure */
/****************************************************************************/
static kl_mem_t mem={.init=0};


/****************************************************************************/
/* Initialize static variables and data structures */
/****************************************************************************/
#define KL_INIT_CHECK                                                       \
do {                                                                        \
  if (0 == mem.init)                                                        \
    kl_mem_init(&mem);                                                      \
} while (0)


/****************************************************************************/
/* Allocate size bytes of memory */
/****************************************************************************/
KL_EXPORT void *
KL_malloc(size_t const size)
{
  //void * brick, * chunk, * ptr=NULL;
  void * ptr=NULL;
  kl_brick_t * brick;

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

  KL_INIT_CHECK;

  if (size > ALLOC_MAX_SIZE)
    return NULL;

  if (NULL != (brick=kl_brick_bin_find(&mem, size))) {
    ptr = (void*)&brick->iface.raw;

    assert(size <= BRICK_MAX_SIZE);
    assert(KL_BRICK == KL_TYPEOF((kl_alloc_t*)brick));
    assert(KL_BRICK_SIZE(size) == KL_G_SIZE((kl_alloc_t*)brick));
  }
#if 0
  else if (NULL != (chunk=kl_chunk_bin_find(&mem, size))) {
    ptr = KL_G_PTR(chunk);

    assert(size <= CHUNK_MAX_SIZE);
    assert(KL_CHUNK == KL_TYPEOF(chunk));
    assert(KL_CHUNK_SIZE(size) <= KL_G_SIZE(chunk));
  }
  else if (NULL != (chunk=kl_chunk_solo(size))) {
    ptr = KL_G_PTR(chunk);

    assert(size <= CHUNK_MAX_SIZE);
    assert(KL_CHUNK == KL_TYPEOF(chunk));
    //assert(KL_CHUNK_SIZE(size) <= KL_G_SIZE(chunk));
    assert(KL_CHUNK_SIZE(size) == KL_G_SIZE(chunk));
  }
#endif

  assert(KL_ISALIGNED(ptr));

  return ptr;
}


/****************************************************************************/
/* Release size bytes of memory */
/****************************************************************************/
KL_EXPORT void
KL_free(void * const ptr)
{
  kl_alloc_t * alloc;

  KL_INIT_CHECK;

  if (NULL == ptr) {}

  alloc = KL_G_ALLOC(ptr);

  switch (KL_TYPEOF(alloc)) {
    case KL_BRICK:
      kl_brick_bin_ad(&mem, (kl_brick_t*)alloc);
      break;
    case KL_CHUNK:
      //kl_chunk_bin_ad(&mem, (kl_chunk_t*)alloc);
      break;
  }
}


/****************************************************************************/
/* Print some memory statistics */
/****************************************************************************/
KL_EXPORT void
KL_malloc_stats(void)
{
  printf("Calls to system allocator = %zu\n", KL_SYS_CTR);
  printf("Maximum concurrent memory = %zu\n", KL_MEM_MAX);
  fflush(stdout);
}
