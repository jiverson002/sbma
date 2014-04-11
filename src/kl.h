#ifndef KL_H
#define KL_H

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define KL_PAGESIZE   ((size_t)(sysconf(_SC_PAGESIZE)))

#define KL_VMEMADD(N)                                     \
  (KL_VMEM  += kl_multup(N, KL_PAGESIZE),                 \
   KL_MXVMEM = KL_VMEM > KL_MXVMEM ? KL_VMEM : KL_MXVMEM)
#define KL_VMEMSUB(N)                                     \
  (KL_VMEM -= kl_multup(N, KL_PAGESIZE))

#define KL_MMAP(SZ)         (KL_VMEMADD(SZ), kl_mmap_aligned(SZ))
#define KL_MUNMAP(PTR, SZ)  (KL_VMEMSUB(SZ), kl_munmap_aligned(PTR, SZ))

typedef uint32_t      u32;
typedef int32_t       i32;
typedef uintptr_t     uptr;
typedef unsigned char uchar;

extern  size_t        KL_VMEM;
extern  size_t        KL_MXVMEM;

#endif
