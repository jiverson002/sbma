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


#include <malloc.h> /* struct mallinfo */
#include <string.h> /* memset */
#include "sbma.h"
#include "vmm.h"


/****************************************************************************/
/*! Return some memory statistics */
/****************************************************************************/
SBMA_EXTERN struct mallinfo
sbma_mallinfo(void)
{
  struct mallinfo mi;

  memset(&mi, 0, sizeof(struct mallinfo));

  mi.smblks   = _vmm_.numipc;  /* received SIGIPC faults */
  mi.ordblks  = _vmm_.numhipc; /* honored SIGIPC faults */

  mi.usmblks  = _vmm_.numrd; /* syspages read from disk */
  mi.fsmblks  = _vmm_.numwr; /* syspages wrote to disk */
  mi.uordblks = _vmm_.numrf; /* read faults */
  mi.fordblks = _vmm_.numwf; /* write faults */

  if (0 == _vmm_.ipc.init) {
    mi.hblks = _vmm_.ipc.curpages;  /* syspages loaded */
  }
  else {
    mi.hblks = _vmm_.ipc.c_mem[_vmm_.ipc.id]; /* ... */
  }
  mi.hblkhd   = _vmm_.ipc.maxpages; /* high water mark for loaded syspages */
  mi.keepcost = _vmm_.numpages;     /* syspages allocated */

  return mi;
}


#ifdef TEST
int
main(int argc, char * argv[])
{
  if (0 == argc || NULL == argv) {}

  return 0;
}
#endif
