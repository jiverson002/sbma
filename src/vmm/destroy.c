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


#include <errno.h>  /* errno library */
#include <signal.h> /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stddef.h> /* NULL, size_t */
#include "common.h"
#include "ipc.h"
#include "lock.h"
#include "sbma.h"
#include "vmm.h"


SBMA_EXTERN int
vmm_destroy(struct vmm * const vmm)
{
  int retval;

  /* Default return value. */
  retval = 0;

  /* Shortcut if vmm is not initialized. */
  if (0 == vmm->init)
    goto RETURN;

  vmm->init = 0;

  /* reset signal handler for SIGSEGV */
  retval = sigaction(SIGSEGV, &(vmm->oldact_segv), NULL);
  ERRCHK(FATAL, -1 == retval);

  /* reset signal handler for SIGIPC */
  retval = sigaction(SIGIPC, &(vmm->oldact_ipc), NULL);
  ERRCHK(FATAL, -1 == retval);

  /* destroy mmu */
  retval = mmu_destroy(&(vmm->mmu));
  ERRCHK(RETURN, 0 != retval);

  /* destroy ipc */
  retval = ipc_destroy(&(vmm->ipc));
  ERRCHK(RETURN, 0 != retval);

  /* destroy vmm lock */
  retval = lock_free(&(vmm->lock));
  ERRCHK(RETURN, 0 != retval);

  /***************************************************************************/
  /* Successful exit -- return 0. */
  /***************************************************************************/
  goto RETURN;

  /***************************************************************************/
  /* Return point -- return. */
  /***************************************************************************/
  RETURN:
  return retval;

  /***************************************************************************/
  /* Fatal error -- an unrecoverable error has occured, the runtime state
   * cannot be reverted to its state before this function was called. */
  /***************************************************************************/
  FATAL:
  FATAL_ABORT(errno);
}
