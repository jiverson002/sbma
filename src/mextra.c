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

  /* Not checking the return value here is a hack which allows this function
   * to be called even after __vmm_destroy__() has been called. */
  /*ret = LOCK_GET(&(vmm.lock));
  if (-1 == ret)
    return mi;*/
  (void)LOCK_GET(&(vmm.lock));

  mi.smblks  = vmm.numrf; /* read faults */
  mi.ordblks = vmm.numwf; /* write faults */

  mi.usmblks = vmm.numrd; /* syspages read from disk */
  mi.fsmblks = vmm.numwr; /* syspages wrote to disk */

  mi.uordblks = vmm.curpages; /* syspages loaded */
  mi.fordblks = vmm.maxpages; /* high water mark for loaded syspages */
  mi.keepcost = vmm.numpages; /* syspages allocated */

  /* Not checking the return value here is a hack which allows this function
   * to be called even after __vmm_destroy__() has been called. */
  /*ret = LOCK_LET(&(vmm.lock));
  if (-1 == ret)
    return mi;*/
  (void)LOCK_LET(&(vmm.lock));

  return mi;
}


/****************************************************************************/
/*! Modify the eligibility of a process for ipc memory tracking */
/****************************************************************************/
extern int
__ooc_eligible__(int const __eligible)
{
  return __ipc_eligible__(&(vmm.ipc), __eligible);
}
