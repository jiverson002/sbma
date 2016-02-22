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


#include <stddef.h> /* size_t */
#include "common.h"
#include "ipc.h"
#include "sbma.h"


/*****************************************************************************/
/*  MP-Unsafe race:rw(ipc->s_mem,ipc->c_mem[ipc->id])                        */
/*            race:wr(ipc->d_mem[ipc->id])                                   */
/*  MT-Unsafe race:rw(ipc->s_mem,ipc->c_mem[ipc->id])                        */
/*                                                                           */
/*  Note:                                                                    */ 
/*    1)  Only other threads from this process will ever modify              */
/*        ipc->d_mem[ipc->id], so the IPC_INTRA_CRICITAL_SECTION is          */
/*        sufficient to make that variable MT-Safe.                          */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  Call from within an IPC_INTER_CRITICAL_SECTION or call after       */
/*        receiving SIGIPC from a process in an IPC_INTER_CRITICAL_SECTION.  */
/*    2)  Functions that READ ipc->d_mem[ipc->id] from a different process   */
/*        SHOULD be aware of the possibility of reading a stale value.       */
/*****************************************************************************/
SBMA_EXTERN void
ipc_atomic_dec(struct ipc * const ipc, size_t const c_pages,
               size_t const d_pages)
{
  /*=========================================================================*/
  IPC_INTRA_CRITICAL_SECTION_BEG(ipc);
  /*=========================================================================*/

  ASSERT(ipc->c_mem[ipc->id] >= c_pages);
  ASSERT(ipc->d_mem[ipc->id] >= d_pages);

  *ipc->s_mem += c_pages;
  ipc->c_mem[ipc->id] -= c_pages;
  ipc->d_mem[ipc->id] -= d_pages;

  /*=========================================================================*/
  IPC_INTRA_CRITICAL_SECTION_END(ipc);
  /*=========================================================================*/
}
