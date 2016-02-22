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


#include <stddef.h> /* NULL, size_t */
#include "common.h"
#include "lock.h"
#include "mmu.h"


/*****************************************************************************/
/*  MT-Invalid                                                               */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  This function MUST be called EXACTLY ONCE BEFORE any other mmu_*   */
/*        function is called.                                                */
/*****************************************************************************/
SBMA_EXTERN int
mmu_init(struct mmu * const mmu, size_t const page_size)
{
  int retval;

  /* Clear pointer. */
  mmu->a_tbl = NULL;

  /* Set mmu page size. */
  mmu->page_size = page_size;

  /* Initialize mmu lock. */
  retval = lock_init(&(mmu->lock));
  ERRCHK(RETURN, 0 != retval);

  RETURN:
  return retval;
}


#ifdef TEST
int
main(int argc, char * argv[])
{
  if (0 == argc || NULL == argv) {}

  return 0;
}
#endif
