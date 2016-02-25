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


#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* size_t */
#include <stdio.h>     /* FILENAME_MAX */
#include <sys/types.h> /* truncate */
#include <unistd.h>    /* truncate */
#include "common.h"
#include "sbma.h"
#include "vmm.h"




/****************************************************************************/
/*! Remap an address range to a new LARGER address range. */
/****************************************************************************/
SBMA_EXTERN int
sbma_remap(void * const __nbase, void * const __obase, size_t const __size)
{
  int ret;
  size_t i, page_size, s_pages;
  volatile uint8_t * oflags, * nflags;
  struct ate * oate, * nate;
  char ofname[FILENAME_MAX], nfname[FILENAME_MAX];

  if (0 == __size)
    return -1;

  page_size = _vmm_.page_size;
  s_pages   = 1+((sizeof(struct ate)-1)/page_size);
  oate      = (struct ate*)((uintptr_t)__obase-(s_pages*page_size));
  oflags    = oate->flags;
  nate      = (struct ate*)((uintptr_t)__nbase-(s_pages*page_size));
  nflags    = nate->flags;

  ASSERT((uintptr_t)__obase == oate->base);
  ASSERT((uintptr_t)__nbase == nate->base);
  ASSERT(oate->n_pages <= nate->n_pages);

  /* Make sure that old memory is stored in file */
  ret = sbma_mevict((void*)oate->base, oate->n_pages*page_size);
  if (-1 == ret)
    return -1;
  /* Make sure that new memory is uninitialized */
  ret = sbma_mclear((void*)nate->base, nate->n_pages*page_size);
  if (-1 == ret)
    return -1;
  /* Make sure that new memory has no read permissions so that it will load
   * from disk any necessary pages. */
  ret = sbma_mevict((void*)nate->base, nate->n_pages*page_size);
  if (-1 == ret)
    return -1;

  for (i=0; i<oate->n_pages; ++i) {
    ASSERT(MMU_DIRTY != (oflags[i]&MMU_DIRTY)); /* not dirty */
    ASSERT(MMU_DIRTY != (nflags[i]&MMU_DIRTY)); /* not dirty */
    ASSERT(MMU_ZFILL != (nflags[i]&MMU_ZFILL)); /* not on disk */

    /* copy zfill bit from old flag */
    nflags[i] |= (oflags[i]&MMU_ZFILL);
  }

  /* move old file to new file and truncate to size. */
  ret = snprintf(nfname, FILENAME_MAX, "%s%d-%zx", _vmm_.fstem, (int)getpid(),\
    (uintptr_t)nate);
  if (ret < 0)
    return -1;
  ret = snprintf(ofname, FILENAME_MAX, "%s%d-%zx", _vmm_.fstem, (int)getpid(),\
    (uintptr_t)oate);
  if (ret < 0)
    return -1;
  ret = rename(ofname, nfname);
  if (-1 == ret)
    return -1;
#if SBMA_FILE_RESERVE == 1
  ret = truncate(nfname, nn_pages*page_size);
  if (-1 == ret)
    return -1;
#endif

  return 0;
}


#ifdef TEST
int
main(int argc, char * argv[])
{
  if (0 == argc || NULL == argv) {}

  return 0;
}
#endif
