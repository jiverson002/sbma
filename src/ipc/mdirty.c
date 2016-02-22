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


#include <stddef.h> /* ssize_t */
#include "common.h"
#include "ipc.h"
#include "sbma.h"


/*****************************************************************************/
/*  MP-Unsafe race:wr(ipc->d_mem[ipc->id])                                   */
/*  MT-Safe                                                                  */
/*                                                                           */
/*  Note:                                                                    */ 
/*    1)  Only other threads from this process will ever modify              */
/*        ipc->d_mem[ipc->id], so the IPC_INTRA_CRICITAL_SECTION is          */
/*        sufficient to make that variable MT-Safe.                          */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  Functions that READ ipc->d_mem[ipc->id] from a different process   */
/*        SHOULD be aware of the possibility of reading a stale value.       */
/*****************************************************************************/
SBMA_EXTERN int
ipc_mdirty(struct ipc * const ipc, ssize_t const value)
{
  if (0 == value)
    return 0;

  /*=========================================================================*/
  IPC_INTRA_CRITICAL_SECTION_BEG(ipc);
  /*=========================================================================*/

  if (value < 0) {
    ASSERT(ipc->d_mem[ipc->id] >= (size_t)(-value));
  }

  ipc->d_mem[ipc->id] += value;

  /*=========================================================================*/
  IPC_INTRA_CRITICAL_SECTION_END(ipc);
  /*=========================================================================*/

  return 0;
}
