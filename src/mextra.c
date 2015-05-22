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
#include "vmm.h"


/****************************************************************************/
/*! Set parameters for the sbmalloc subsystem. */
/****************************************************************************/
extern int
__ooc_mallopt__(int const param, int const value)
{
  int ret;

  ret = LOCK_GET(&(vmm.lock));
  if (-1 == ret)
    return -1;

  switch (param) {
    case M_VMMOPTS:
    vmm.opts = value;
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
