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


#include <errno.h>     /* errno library */
#include <fcntl.h>     /* O_RDWR, O_CREAT, O_EXCL */
#include <stdint.h>    /* uint8_t, uintptr_t */
#include <stddef.h>    /* NULL, size_t, SIZE_MAX */
#include <stdio.h>     /* FILENAME_MAX */
#include <sys/mman.h>  /* mmap */
#include <sys/stat.h>  /* S_IRUSR, S_IWUSR */
#include <sys/types.h> /* ftruncate */
#include <unistd.h>    /* ftruncate */
#include "common.h"
#include "ipc.h"
#include "sbma.h"


/*****************************************************************************/
/*  MP-Safe                                                                  */
/*  MT-Invalid                                                               */
/*                                                                           */
/*  Mitigation:                                                              */
/*    1)  This function MUST be called EXACTLY ONCE BEFORE any other ipc_*   */
/*        function is called.                                                */
/*****************************************************************************/
SBMA_EXTERN int
ipc_init(struct ipc * const ipc, int const uniq, int const n_procs,
         size_t const max_mem)
{
  int ret, shm_fd, id;
  void * shm;
  sem_t * inter_mtx, * done, * sid, * sig;
  int * idp;
  char fname[FILENAME_MAX];

  /* initialize semaphores */
  #define X(SEM, INIT)\
    if (0 > snprintf(fname, FILENAME_MAX, "/ipc-" #SEM "-%d", uniq))\
      return -1;\
    SEM = sem_open(fname, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR, INIT);\
    if (SEM_FAILED == SEM)\
      return -1;
  LIST_OF_SEMAPHORES
  #undef X

  /* set up thread mutex */
  ret = pthread_mutex_init(&(ipc->intra_mtx), NULL);
  if (-1 == ret)
    return -1;
  /* try to create a new shared memory region -- if i create, then i should
   * also truncate it, if i dont create, then try and just open it. */
  if (0 > snprintf(fname, FILENAME_MAX, "/ipc-shm-%d", uniq))
    return -1;

  /* Set up shared memory region. */
  shm_fd = shm_open(fname, O_RDWR|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR);
  if (-1 == shm_fd) {
    if (EEXIST == errno) {
      shm_fd = shm_open(fname, O_RDWR, S_IRUSR|S_IWUSR);
      if (-1 == shm_fd)
        return -1;
    }
    else {
      return -1;
    }
  }
  else {
    ret = ftruncate(shm_fd, IPC_LEN(n_procs));
    if (-1 == ret)
      return -1;

    /* initialize system memory counter */
    ret = write(shm_fd, &max_mem, sizeof(size_t));
    if (-1 == ret)
      return -1;
  }

  /* Map the shared memory region into my address space. */
  shm = mmap(NULL, IPC_LEN(n_procs), PROT_READ|PROT_WRITE, MAP_SHARED,\
    shm_fd, 0);
  if (MAP_FAILED == shm)
    return -1;

  /* Close the file descriptor. */
  ret = close(shm_fd);
  if (-1 == ret)
    return -1;

  /* Begin critical section. */
  ret = sem_wait(sid);
  if (-1 == ret)
    return -1;

  /* id pointer is last sizeof(int) bytes of shm */
  idp = (int*)((uintptr_t)shm+sizeof(size_t)+\
    (n_procs*(sizeof(int)+sizeof(size_t)+sizeof(size_t))));
  id = (*idp)++;

  /* End critical section. */
  ret = sem_post(sid);
  if (-1 == ret)
    return -1;

  /* Sanity check. */
  ASSERT(id < n_procs);

  /* Setup ipc struct. */
  ipc->id        = id;
  ipc->n_procs   = n_procs;
  ipc->uniq      = uniq;
  ipc->curpages  = 0;
  ipc->maxpages  = 0;
  ipc->shm       = shm;
  ipc->inter_mtx = inter_mtx;
  ipc->done      = done;
  ipc->sid       = sid;
  ipc->sig       = sig;
  ipc->s_mem     = (size_t*)shm;
  ipc->c_mem     = (size_t*)((uintptr_t)ipc->s_mem+sizeof(size_t));
  ipc->d_mem     = (size_t*)((uintptr_t)ipc->c_mem+(n_procs*sizeof(size_t)));
  ipc->pid       = (int*)((uintptr_t)ipc->d_mem+(n_procs*sizeof(size_t)));
  ipc->flags     = (uint8_t*)((uintptr_t)idp+sizeof(int));

  /* Set my process id. */
  ipc->pid[id] = (int)getpid();

  return 0;
}
