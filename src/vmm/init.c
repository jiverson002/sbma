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


#include <errno.h>    /* errno library */
#include <signal.h>   /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stddef.h>   /* NULL, size_t */
#include <stdint.h>   /* uint8_t, uintptr_t */
#include <stdio.h>    /* FILENAME_MAX */
#include <string.h>   /* strncpy */
#include <sys/mman.h> /* mprotect */
#include <time.h>     /* struct timespec */
#include "common.h"
#include "ipc.h"
#include "lock.h"
#include "mmu.h"
#include "sbma.h"
#include "vmm.h"


/*****************************************************************************/
/*  SIGSEGV handler.                                                         */
/*                                                                           */
/*  MT-Safe                                                                  */
/*****************************************************************************/
SBMA_STATIC void
vmm_sigsegv(int const sig, siginfo_t * const si, void * const ctx)
{
  int ret;
  size_t ip, page_size, _len;
  uintptr_t addr;
  void * _addr;
  volatile uint8_t * flags;
  struct ate * ate;

  /* make sure we received a SIGSEGV */
  ASSERT(SIGSEGV == sig);

  /* setup local variables */
  page_size = _vmm_.page_size;
  addr      = (uintptr_t)si->si_addr;

  /* lookup allocation table entry */
  ate = mmu_lookup_ate(&(_vmm_.mmu), (void*)addr);
  ASSERT((struct ate*)-1 != ate);
  ASSERT(NULL != ate);

  ip    = (addr-ate->base)/page_size;
  flags = ate->flags;

  if (MMU_RSDNT == (flags[ip]&MMU_RSDNT)) {
    if (VMM_LZYRD == (_vmm_.opts&VMM_LZYRD)) {
      _addr = (void*)(ate->base+ip*page_size);
      _len  = page_size;
    }
    else {
      _addr = (void*)ate->base;
      _len  = ate->n_pages*page_size;
    }

    ret = sbma_mtouch(ate, _addr, _len);
    ASSERT(-1 != ret);

    ret = lock_let(&(ate->lock));
    ASSERT(-1 != ret);

    VMM_INTRA_CRITICAL_SECTION_BEG(&_vmm_);
    VMM_TRACK(&_vmm_, numrf, 1);
    VMM_INTRA_CRITICAL_SECTION_END(&_vmm_);
  }
  else {
    /* sanity check */
    ASSERT(MMU_DIRTY != (flags[ip]&MMU_DIRTY)); /* not dirty */

    /* flag: 100 */
    flags[ip] = MMU_DIRTY;

    /* update protection to read-write */
    ret = mprotect((void*)(ate->base+(ip*page_size)), page_size,\
      PROT_READ|PROT_WRITE);
    ASSERT(-1 != ret);

    /* release lock on alloction table entry */
    ret = lock_let(&(ate->lock));
    ASSERT(-1 != ret);

    /* increase count of dirty pages */
    ate->d_pages++;
    ret = ipc_mdirty(&(_vmm_.ipc), VMM_TO_SYS(1));
    ASSERT(-1 != ret);

    VMM_INTRA_CRITICAL_SECTION_BEG(&_vmm_);
    VMM_TRACK(&_vmm_, numwf, 1);
    VMM_INTRA_CRITICAL_SECTION_END(&_vmm_);
  }

  if (NULL == ctx) {} /* suppress unused warning */
}


/*****************************************************************************/
/*  SIGIPC handler.                                                          */
/*                                                                           */
/*  MT-Safe                                                                  */
/*****************************************************************************/
SBMA_STATIC void
vmm_sigipc(int const sig, siginfo_t * const si, void * const ctx)
{
  int ret;
  size_t c_pages, d_pages, numwr;
  struct timespec tmr;

  /* Sanity check: make sure we received a SIGIPC. */
  ASSERT(SIGIPC <= SIGRTMAX);
  ASSERT(SIGIPC == sig);

  /* Only honor the SIGIPC if my status is still eligible. */
  if (ipc_is_eligible(&(_vmm_.ipc), _vmm_.ipc.id)) {
    /* XXX Is it possible / what happens / does it matter if the process
     * receives a SIGIPC at this point in the execution of the signal handler?
     * */
    /* XXX This is not possible according to signal.h man pages. There it
     * states that ``In addition, the signal which triggered the handler will
     * be blocked, unless the SA_NODEFER flag is used.''. In the current code,
     * SA_NODEFER is not used. */

    /*=======================================================================*/
    TIMER_START(&(tmr));
    /*=======================================================================*/

    /* Evict all memory. */
    ret = sbma_mevictall_int(&c_pages, &d_pages, &numwr);
    ASSERT(-1 != ret);

    /* Update ipc memory statistics. */
    ipc_atomic_dec(&(_vmm_.ipc), c_pages, d_pages);

    /*=======================================================================*/
    TIMER_STOP(&(tmr));
    /*=======================================================================*/

    /* Track number of number of syspages written to disk, time taken for
     * writing, and number of SIGIPC honored. */
    VMM_INTRA_CRITICAL_SECTION_BEG(&_vmm_);
    VMM_TRACK(&_vmm_, numwr, numwr);
    VMM_TRACK(&_vmm_, tmrwr, (double)tmr.tv_sec+(double)tmr.tv_nsec/1000000000.0);
    VMM_TRACK(&_vmm_, numhipc, 1);
    VMM_INTRA_CRITICAL_SECTION_END(&_vmm_);
  }

  /* Signal to the waiting process that the memory has been released. */
  ret = sem_post(_vmm_.ipc.done);
  ASSERT(-1 != ret);

  /* Track the number of SIGIPC received. */
  VMM_INTRA_CRITICAL_SECTION_BEG(&_vmm_);
  VMM_TRACK(&_vmm_, numipc, 1);
  VMM_INTRA_CRITICAL_SECTION_END(&_vmm_);

  if (NULL == si || NULL == ctx) {}
}


SBMA_EXTERN int
vmm_init(struct vmm * const vmm, char const * const fstem, int const uniq,
         size_t const page_size, int const n_procs, size_t const max_mem,
         int const opts)
{
  int retval;

  /* Default return value. */
  retval = 0;

  /* Shortcut if vmm is already initialized. */
  if (1 == vmm->init)
    goto RETURN;
  /* Shortcut if opts are invalid. */
  if (VMM_INVLD == (opts&VMM_INVLD))
    goto ERREXIT;

  /* Set page size. */
  vmm->page_size = page_size;

  /* Set options. */
  vmm->opts = opts;

  /* Initialize statistics. */
  vmm->numipc   = 0;
  vmm->numhipc  = 0;
  vmm->numrf    = 0;
  vmm->numwf    = 0;
  vmm->numrd    = 0;
  vmm->numwr    = 0;
  vmm->tmrrd    = 0.0;
  vmm->tmrwr    = 0.0;
  vmm->numpages = 0;

  /* Copy file stem. */
  strncpy(vmm->fstem, fstem, FILENAME_MAX-1);
  vmm->fstem[FILENAME_MAX-1] = '\0';

  /* Setup the signal handler for SIGSEGV. */
  vmm->act_segv.sa_flags     = SA_SIGINFO;
  vmm->act_segv.sa_sigaction = vmm_sigsegv;
  retval = sigemptyset(&(vmm->act_segv.sa_mask));
  ERRCHK(FATAL, -1 == retval);
  retval = sigaction(SIGSEGV, &(vmm->act_segv), &(vmm->oldact_segv));
  ERRCHK(FATAL, -1 == retval);

  /* Setup the signal handler for SIGIPC. */
  vmm->act_ipc.sa_flags     = SA_SIGINFO;
  vmm->act_ipc.sa_sigaction = vmm_sigipc;
  retval = sigemptyset(&(vmm->act_ipc.sa_mask));
  ERRCHK(FATAL, -1 == retval);
  retval = sigaction(SIGIPC, &(vmm->act_ipc), &(vmm->oldact_ipc));
  ERRCHK(FATAL, -1 == retval);

  /* Initialize mmu. */
  retval = mmu_init(&(vmm->mmu), page_size);
  ERRCHK(FATAL, -1 == retval);

  /* Initialize ipc. */
  retval = ipc_init(&(vmm->ipc), uniq, n_procs, max_mem);
  ERRCHK(FATAL, -1 == retval);

  /* Initialize vmm lock. */
  retval = lock_init(&(vmm->lock));
  ERRCHK(FATAL, -1 == retval);

  vmm->init = 1;

  /***************************************************************************/
  /* Successful exit -- return 0. */
  /***************************************************************************/
  goto RETURN;

  /***************************************************************************/
  /* Error exit -- return -1. */
  /***************************************************************************/
  ERREXIT:
  retval = -1;

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
