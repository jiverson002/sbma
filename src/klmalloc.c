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


#ifndef _BSD_SOURCE
# define _BSD_SOURCE
#endif
#include <sys/mman.h> /* mmap */
#undef _BSD_SOURCE

#include <assert.h>   /* assert */
#include <stdint.h>   /* uint*_t */
#include <stddef.h>   /* size_t */

#include "kldpq.h"


/****************************************************************************/
/* System memory allocation related macros */
/****************************************************************************/
#define KL_MMAP_FAIL        MAP_FAILED
#define KL_CALL_MMAP(S)     mmap(NULL, S, PROT_READ|PROT_WRITE, \
                              MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
#define KL_CALL_MUNMAP(P,S) munmap(P, S)


/****************************************************************************/
/* Relevant types */
/****************************************************************************/
typedef uintptr_t uptr;
typedef unsigned char uchar;


/****************************************************************************/
/* Bit-set */
/****************************************************************************/
#define B1         (1)
#define BSIZ       (sizeof(uchar)*CHAR_BIT)
#define bsiz(N)    ((N+BSIZ-1)/BSIZ)
#define bbyt(N)    (bsiz(N)*sizeof(uchar))
#define bget(B, I) (((B)[(I)/BSIZ])&(B1<<((I)%BSIZ)))
#define bset(B, I) (((B)[(I)/BSIZ])|=(B1<<((I)%BSIZ)))
#define buns(B, I) (((B)[(I)/BSIZ])&=~(B1<<((I)%BSIZ)))


/****************************************************************************/
/* Access macros for a memory allocation block */
/****************************************************************************/
#define KLMEMMSIZE     KLMAXSIZE
#define KLMEMASIZE     (8+sizeof(kl_dpq_node_t)+(KLMEMSIZE/8)+KLMEMMSIZE)
#define KLMEMFLAG(MEM) (*(uchar*)(MEM))
#define KLMEMPTR(MEM)  ((kl_dpq_node_t*)((uintptr_t)(MEM)+8))
#define KLMEMBIT(MEM)  ((uchar*)((uintptr_t)KLMEMPTR(MEM)+sizeof(kl_dpq_node_t)))
#define KLMEMMEM(MEM)  ((void*)((uintptr_t)KLMEMBIT(MEM)++KLMEMSIZE/8))


static kl_dpq_t dpq;


/****************************************************************************/
/* System allocate aligned memory */
/****************************************************************************/
static void *
kl_sys_alloc_aligned(size_t const size, size_t const align)
{
  uintptr_t mask;
  void * mem, * ptr;

  assert(0 == (align&(align-1)));

  mask = ~(uintptr_t)(align-1);
  mem  = KL_CALL_MMAP(size+align-1);

  if (KL_MMAP_FAIL == mem)
    return NULL;

  ptr = (void *)(((uintptr_t)mem+align-1)&mask);
  assert(0 == ((uintptr_t)ptr&align));

  /* unmap memory up to ptr and anything after ptr+size */
  /* TODO: this will break xmmalloc */
  if (mem != ptr)
    KL_CALL_MUNMAP(mem, (uintptr_t)ptr-(uintptr_t)mem);
  KL_CALL_MUNMAP((void*)((uintptr_t)ptr+size),
    (uintptr_t)mem+size+align-1-(uintptr_t)ptr);

  return ptr;
}


/****************************************************************************/
/* Allocate size bytes of memory */
/****************************************************************************/
extern void *
klmalloc(size_t const size)
{
  int bidx;
  void * ptr, * mem;

  if (size > KLMAXSIZE) {
    /* large allocations are serviced directly by system */
    return kl_sys_alloc_aligned(size, KLMEMMSIZE);
  }
  else {
    /* try to get a previously allocated block of memory */
    if (-1 != (bidx=kl_dpq_find(&dpq, size))) {
      /* if no previously allocated block of memory can support this
       * allocation, then allocate a new block */
      mem = kl_sys_alloc_aligned(size, KLMEMMSIZE);
      if (NULL == mem)
        return NULL;

      /* add new block to DPQ */
      kl_dpq_ad(&dpq, KLMEMPTR(mem));

      bidx = KLMAXBIN;
    }

    /* get a pointer from bin[bidx] in the DPQ */
  }

  //return ptr;
  return NULL;
}


#if 0
/*****************************************************************************/
/* allocate and zero-initalize an array of num*size bytes */
/*****************************************************************************/
extern void *
klcalloc(size_t const num, size_t const size)
{
  void * ptr = klmalloc(num*size);
  memset(ptr, 0, num*size);
  return ptr;
}


/*****************************************************************************/
/* resize memory block pointed to by ptr to be sz bytes */
/*****************************************************************************/
extern void *
klrealloc(void * const ptr, size_t const sz)
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
/* deallocate memory block */
/*****************************************************************************/
extern void
klfree(void * const ptr)
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

    buns(b->blockPtr+asz, eid);

    kl_dpq_dec(&bp->blockQ, bp->blockMap[bid]);
    if (0 == bp->blockMap[bid]->k) {
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
      }
      else if (bp->poolSiz > 1 && bp->poolAct*1.0/bp->poolSiz <= BPOOL_TRSH) {
        bpool_shrink(pid);
      }
#endif
    }

    if (0 == (--mpool.poolCtr))
      mpool_free();
  }
}
#endif
