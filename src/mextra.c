#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif


#ifdef NDEBUG
# undef NDEBUG
#endif


#include <malloc.h> /* struct mallinfo */
#include <string.h> /* memset */
#include "config.h"
#include "sbma.h"
#include "ipc.h"
#include "vmm.h"


/****************************************************************************/
/*! Set parameters for the sbmalloc subsystem. */
/****************************************************************************/
extern int
__ooc_mallopt__(int const __param, int const __value)
{
  int ret;

  ret = LOCK_GET(&(vmm.lock));
  if (-1 == ret)
    return -1;

  switch (__param) {
    case M_VMMOPTS:
    vmm.opts = __value;
    break;

    default:
    return -1;
  }

  ret = LOCK_LET(&(vmm.lock));
  if (-1 == ret)
    return -1;

  return 0;
}


/****************************************************************************/
/*! Return some memory statistics */
/****************************************************************************/
extern struct mallinfo
__ooc_mallinfo__(void)
{
  int ret;
  struct mallinfo mi;

  memset(&mi, 0, sizeof(struct mallinfo));

  ret = LOCK_GET(&(vmm.lock));
  if (-1 == ret)
    return mi;

  mi.smblks  = vmm.numrf; /* read faults */
  mi.ordblks = vmm.numwf; /* write faults */

  mi.usmblks = vmm.numrd; /* syspages read from disk */
  mi.fsmblks = vmm.numwr; /* syspages wrote to disk */

  mi.uordblks = vmm.curpages; /* syspages loaded */
  mi.fordblks = vmm.maxpages; /* high water mark for loaded syspages */
  mi.keepcost = vmm.numpages; /* syspages allocated */

  ret = LOCK_LET(&(vmm.lock));
  if (-1 == ret)
    return mi;

  return mi;
}


/****************************************************************************/
/*! Modify the eligiblity of a process for ipc memory tracking */
/****************************************************************************/
extern int
__ooc_memcpy__(void * __dst, void const * __src, size_t __num)
{
#if 0
  int ret;
  size_t i, diff;
  ssize_t d_beg, d_end, s_beg, s_end;
  uint8_t * d_flags, * s_flags;
  struct ate * d_ate, * s_ate;

  d_ate = __mmu_lookup_ate__(&(vmm.mmu), __dst);
  if (NULL == d_ate)
    return -1;
  s_ate = __mmu_lookup_ate__(&(vmm.mmu), __src);
  if (NULL == s_ate)
    return -1;

  page_size = vmm.page_size;
  d_flags   = d_ate->flags;
  s_flags   = s_ate->flags;

  /* need to make sure that all bytes are captured, thus beg is a floor
   * operation and end is a ceil operation. */
  d_beg = ((uintptr_t)__dst-d_ate->base)/page_size;
  d_end = 1+(((uintptr_t)__dst+__num-d_ate->base-1)/page_size);
  d_beg = ((uintptr_t)__src-s_ate->base)/page_size;
  d_end = 1+(((uintptr_t)__src+__num-s_ate->base-1)/page_size);

  /* copy bytes of first page separately, in case the page is not copied
   * entirely. */
  diff = (uintptr_t)src-(uintptr_t)__ate->base+beg*page_size;
  if (0 != diff) {
    if (MMU_RSDNT != (flags[s_beg]&MMU_RSDNT)) { /* is resident */
      /* flag: 100 */
      flags[d_beg] = MMU_DIRTY;

      /* update protection to read-write */
      ret = mprotect((void*)(d_ate->base+(d_beg*page_size)), page_size,\
        PROT_READ|PROT_WRITE);
      if (-1 == ret) {
        (void)LOCK_LET(&(d_ate->lock));
        return -1;
      }

      memcpy(__dst, __src, diff);
    }

    __dst += diff;
    __src += diff;
    __num -= diff;
    d_beg++;
    s_beg++;
  }

  /* copy bytes of last page separately, in case the page is not copied
   * entirely. */
  diff = (uintptr_t)__ate->base+end*page_size-(uintptr_t)__src+__num;
  if (0 != diff) {
    if (MMU_RSDNT != (flags[s_end-1]&MMU_RSDNT)) { /* is resident */
      /* flag: 100 */
      flags[d_end-1] = MMU_DIRTY;

      /* update protection to read-write */
      ret = mprotect((void*)(d_ate->base+((d_end-1)*page_size)), page_size,\
        PROT_READ|PROT_WRITE);
      if (-1 == ret) {
        (void)LOCK_LET(&(d_ate->lock));
        return -1;
      }

      memcpy((void*)(uintptr_t)__dst+__num-diff),\
        (void*)((uintptr_t)__src+__num-diff, diff);
    }

    __num -= diff;
    d_end--;
    s_end--;
  }

  if (0 != __num) {
    for (i=0; i<__num; i+=page_size) {
      if (MMU_RSDNT != (flags[s_beg]&MMU_RSDNT)) { /* is resident */
        /* flag: 100 */
        flags[d_beg] = MMU_DIRTY;

        /* update protection to read-write */
        ret = mprotect((void*)(d_ate->base+(d_beg*page_size)), page_size,\
          PROT_READ|PROT_WRITE);
        if (-1 == ret) {
          (void)LOCK_LET(&(d_ate->lock));
          return -1;
        }

        memcpy(__dst, __src, page_size);
      }

      __dst += page_size;
      __src += page_size;
      d_beg++;
      s_beg++;
    }
  }

  if (-1 == LOCK_LET(&(d_ate->lock)))
    return -1;
  if (-1 == LOCK_LET(&(s_ate->lock)))
    return -1;
#else
  memcpy(__dst, __src, __num);
#endif

  return 0;
}


/****************************************************************************/
/*! Modify the eligibility of a process for ipc memory tracking */
/****************************************************************************/
extern int
__ooc_eligible__(int const __eligible)
{
  return __ipc_eligible__(&(vmm.ipc), __eligible);
}
