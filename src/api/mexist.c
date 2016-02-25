/*
Copyright (c) 2015,2016 Jeremy Iverson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/


#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif


#include <stddef.h>    /* NULL */
#include "common.h"
#include "lock.h"
#include "mmu.h"
#include "sbma.h"
#include "vmm.h"


/****************************************************************************/
/*! Check if __addr exists in an allocation table entry. */
/****************************************************************************/
SBMA_EXTERN int
sbma_mexist(void const * const __addr)
{
  int ret;
  struct ate * ate;

  if (0 == _vmm_.init)
    return 0;

  ate = mmu_lookup_ate(&(_vmm_.mmu), __addr);
  if ((struct ate*)-1 == ate)
    return -1;
  else if (NULL == ate)
    return 0;

  ret = lock_let(&(ate->lock));
  if (-1 == ret)
    goto CLEANUP;

  return 1;

  CLEANUP:
  ret = lock_let(&(ate->lock));
  ASSERT(-1 != ret);
  return -1;
}


#ifdef TEST
int
main(int argc, char * argv[])
{
  if (0 == argc || NULL == argv) {}

  return 0;
}
#endif
