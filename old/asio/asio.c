#ifndef _POSIC_C_SOURCE
# define _POSIX_C_SOURCE 200112L
#endif


/*--------------------------------------------------------------------------*/
/*  INCLUDES                                                                */
/*--------------------------------------------------------------------------*/
#include <assert.h>   /* assert */
#include <errno.h>    /* errno */
#include <pthread.h>  /* pthread library */
#include <signal.h>   /* SIGKILL */
#include <stdio.h>    /* stderr, fprintf */
#include <stdlib.h>   /* NULL, abort */
#include <string.h>   /* strerror */
#include <time.h>     /* nanosleep */

#include "asio.h"     /* asynchronous I/O */


/*--------------------------------------------------------------------------*/
/*  MACROS                                                                  */
/*--------------------------------------------------------------------------*/
#define ASIO_TIMEOUT 10

#define _ASIO_GET_LOCK(LOCK)                                                \
do {                                                                        \
  int retval;                                                               \
  if (0 != (retval=pthread_mutex_lock(LOCK))) {                             \
    ASIO_ERROR("Mutex lock failed [retval: %d %s]\n", retval,               \
      strerror(retval));                                                    \
  }                                                                         \
} while (0)

#define _ASIO_GET_COND(COND, LOCK)                                          \
do {                                                                        \
  int retval;                                                               \
  if (0 != (retval=pthread_cond_wait(COND, LOCK))) {                        \
    ASIO_ERROR("Mutex conitional timed wait failed [retval: %d %s]\n",      \
      retval, strerror(retval));                                            \
  }                                                                         \
} while (0)

#define ASIO_WARN(FMT, ...)                                                 \
do {                                                                        \
  int oldstate;                                                             \
  pthread_setcanceltype(PTHREAD_CANCEL_DISABLE, &oldstate);                 \
  fprintf(stderr, "asio: warning: %s:%d: ", __FILE__, __LINE__);            \
  fprintf(stderr, FMT, __VA_ARGS__);                                        \
  pthread_setcanceltype(oldstate, &oldstate);                               \
} while (0)

#define ASIO_ERROR(FMT, ...)                                                \
do {                                                                        \
  int oldstate;                                                             \
  pthread_setcanceltype(PTHREAD_CANCEL_DISABLE, &oldstate);                 \
  fprintf(stderr, "asio: error: %s:%d: ", __FILE__, __LINE__);              \
  fprintf(stderr, FMT, __VA_ARGS__);                                        \
  abort();                                                                  \
} while (0)

#ifdef NDEBUG
# define ASIO_GET_LOCK(LOCK) _ASIO_GET_LOCK(LOCK)
#else
# define ASIO_GET_LOCK(LOCK)                                                \
  do {                                                                      \
    int retval;                                                             \
    struct timespec ts;                                                     \
    if (0 != clock_gettime(CLOCK_REALTIME, &ts)) {                          \
      ASIO_ERROR("Clock get time failed [errno: %d %s]\n", errno,           \
        strerror(errno));                                                   \
    }                                                                       \
    ts.tv_sec += ASIO_TIMEOUT;                                              \
    if (0 != (retval=pthread_mutex_timedlock(LOCK, &ts))) {                 \
      if (ETIMEDOUT == retval) {                                            \
        ASIO_WARN("Mutex lock timed-out [%s:%d]\n", __FILE__, __LINE__);    \
        _ASIO_GET_LOCK(LOCK);                                               \
        ASIO_WARN("Mutex unlocked after it timed-out [%s:%d]\n", __FILE__,  \
          __LINE__);                                                        \
      }                                                                     \
      else {                                                                \
        ASIO_ERROR("Mutex lock failed [retval: %d %s]\n", retval,           \
          strerror(retval));                                                \
      }                                                                     \
    }                                                                       \
  } while (0)
#endif

#define ASIO_LET_LOCK(LOCK)                                                 \
do {                                                                        \
  int retval;                                                               \
  if (0 != (retval=pthread_mutex_unlock(LOCK))) {                           \
    ASIO_ERROR("Mutex unlock failed [retval: %d %s]\n", retval,             \
      strerror(retval));                                                    \
  }                                                                         \
} while (0)

#ifdef NDEBUG
# define ASIO_GET_COND(COND, LOCK) _ASIO_GET_COND(COND, LOCK)
#else
# define ASIO_GET_COND(COND, LOCK)                                          \
  do {                                                                      \
    int retval;                                                             \
    struct timespec ts;                                                     \
    if (0 != clock_gettime(CLOCK_REALTIME, &ts)) {                          \
      ASIO_ERROR("Clock get time failed [errno: %d %s]\n", errno,           \
        strerror(errno));                                                   \
    }                                                                       \
    ts.tv_sec += ASIO_TIMEOUT;                                              \
    if (0 != (retval=pthread_cond_timedwait(COND, LOCK, &ts))) {            \
      if (ETIMEDOUT == retval) {                                            \
        ASIO_WARN("Condition wait timed-out [%s:%d]\n", __FILE__,           \
          __LINE__);                                                        \
        _ASIO_GET_LOCK(LOCK);                                               \
        ASIO_WARN("Condition received after it timed-out [%s:%d]\n",        \
          __FILE__, __LINE__);                                              \
      }                                                                     \
      else {                                                                \
        ASIO_ERROR("Condition wait lock failed [retval: %d %s]\n", retval,  \
          strerror(retval));                                                \
      }                                                                     \
    }                                                                       \
  } while (0)
#endif

#define ASIO_LET_COND(COND)                                                 \
do {                                                                        \
  int retval;                                                               \
  if (0 != (retval=pthread_cond_signal(COND))) {                            \
    ASIO_ERROR("Mutex conitional signal failed [retval: %d %s]\n", retval,  \
      strerror(retval));                                                    \
  }                                                                         \
} while (0)

#define ASIO_BARRIER(BAR)                                                   \
do {                                                                        \
  int retval;                                                               \
  if (0 != (retval=pthread_barrier_wait(BAR)) &&                            \
      PTHREAD_BARRIER_SERIAL_THREAD != retval)                              \
  {                                                                         \
    ASIO_ERROR("Barrier failed [retval: %d %s]\n", retval,                  \
      strerror(retval));                                                    \
  }                                                                         \
} while (0)


/*--------------------------------------------------------------------------*/
/*  STATIC FUNCTIONS                                                        */
/*--------------------------------------------------------------------------*/
static void _asio_enq_cancel(void * const arg)
{
  struct asio_env * const asio=(struct asio_env *)arg;

	ASIO_LET_COND(&(asio->wr));
  ASIO_LET_LOCK(&(asio->mtx));
}

static void _asio_deq_cancel(void * const arg)
{
  struct asio_env * const asio=(struct asio_env *)arg;

	ASIO_LET_COND(&(asio->rd));
  ASIO_LET_LOCK(&(asio->mtx));
}

static void _asio_enq(struct asio_env * const asio, void * const work)
{
  pthread_cleanup_push(_asio_enq_cancel, asio);
  ASIO_GET_LOCK(&(asio->mtx));
  while (asio->n == asio->sz)
    ASIO_GET_COND(&(asio->wr), &(asio->mtx));

  assert(asio->n != asio->sz);

  if (asio->tl == asio->sz)
    asio->tl = 0;
  asio->q[asio->tl++] = work;
  asio->n++;

  assert(asio->n <= asio->sz);

	ASIO_LET_COND(&(asio->rd));
  ASIO_LET_LOCK(&(asio->mtx));
  pthread_cleanup_pop(0);
}

static void _asio_deq(struct asio_env * const asio, void ** const work)
{
  pthread_cleanup_push(_asio_deq_cancel, asio);
  ASIO_GET_LOCK(&(asio->mtx));
  while (0 == asio->n)
    ASIO_GET_COND(&(asio->rd), &(asio->mtx));

  assert(0 != asio->n);

  if (asio->hd == asio->sz)
    asio->hd = 0;
  *work = asio->q[asio->hd++];
  asio->n--;

  assert(asio->n < asio->sz);

	ASIO_LET_COND(&(asio->wr));
  ASIO_LET_LOCK(&(asio->mtx));
  pthread_cleanup_pop(0);
}

static void * _asio_start(void * const arg)
{
  void * work=NULL;
  struct asio_env * const asio=(struct asio_env *)arg;

  /* Without this barrier here, it appears that the threads somehow get
   * spawned before the pthread mutex and conditional variables are ready.
   * Not sure how this is possible, but based on valgrind output, it seems to
   * be the case. */
  ASIO_BARRIER(&(asio->bar));

  for (;;) {
    /* Wait until work is availabe, then dequeue it */
    _asio_deq(asio, &work);

    /* Work on chunk of work */
    (*(asio->cb))(work);
  }

  return NULL;
}


/*--------------------------------------------------------------------------*/
/*  EXTERN FUNCTIONS                                                        */
/*--------------------------------------------------------------------------*/
extern int asio_init(struct asio_env * const asio,
                     unsigned int const nthreads,
                     unsigned int const size, void (*cb)(void *))
{
  unsigned int i, chkpt=0;

  if (NULL == asio || 0 == size || 0 == nthreads || NULL == cb)
    return -1;

  asio->nt = nthreads;
  asio->sz = size;
  asio->n  = 0;
  asio->hd = 0;
  asio->tl = 0;
  asio->cb = cb;

  if (NULL == (asio->threads=malloc(nthreads*sizeof(pthread_t))))
    goto CLEANUP;
  chkpt++;
  if (NULL == (asio->q=malloc(size*sizeof(void *))))
    goto CLEANUP;
  chkpt++;
  if (0 != pthread_cond_init(&(asio->rd), NULL))
    goto CLEANUP;
  chkpt++;
  if (0 != pthread_cond_init(&(asio->wr), NULL))
    goto CLEANUP;
  chkpt++;
  if (0 != pthread_mutex_init(&(asio->mtx), NULL))
    goto CLEANUP;
  chkpt++;
  if (0 != pthread_barrier_init(&(asio->bar), NULL, nthreads+1))
    goto CLEANUP;
  chkpt++;

  for (i=0; i<nthreads; ++i) {
    if (0 != pthread_create(&(asio->threads[i]), NULL, _asio_start, asio))
      goto CLEANUP;
    chkpt++;
  }

  /* Without this barrier here, it appears that the threads somehow get
   * spawned before the pthread mutex and conditional variables are ready.
   * Not sure how this is possible, but based on valgrind output, it seems to
   * be the case. */
  ASIO_BARRIER(&(asio->bar));

  return 0;

CLEANUP:
  switch (chkpt) {
  default:
    for (i=0; i<chkpt-7; ++i)
      (void)pthread_kill(asio->threads[i], SIGKILL);
  case 6:
    (void)pthread_barrier_destroy(&(asio->bar));
  case 5:
    (void)pthread_mutex_destroy(&(asio->mtx));
  case 4:
    (void)pthread_cond_destroy(&(asio->wr));
  case 3:
    (void)pthread_cond_destroy(&(asio->rd));
  case 2:
    free(asio->q);
  case 1:
    free(asio->threads);
  case 0:
    break;
  }
  return -1;
}

extern int asio_free(struct asio_env * const asio)
{
  unsigned int i;

  for (i=0; i<asio->nt; ++i)  /* cancel all threads */
    if (0 != pthread_cancel(asio->threads[i]))
      goto CLEANUP;
  for (i=0; i<asio->nt; ++i)  /* try to join nicely */
    if (0 != pthread_join(asio->threads[i], NULL))
      goto CLEANUP;

  free(asio->threads);
  free(asio->q);

  if (0 != pthread_cond_destroy(&(asio->rd)))
    goto CLEANUP;
  if (0 != pthread_cond_destroy(&(asio->wr)))
    goto CLEANUP;
  if (0 != pthread_mutex_destroy(&(asio->mtx)))
    goto CLEANUP;
  if (0 != pthread_barrier_destroy(&(asio->bar)))
    goto CLEANUP;

  return 0;

CLEANUP:
  for (i=0; i<asio->nt; ++i)  /* make sure they are dead */
    (void)pthread_kill(asio->threads[i], SIGKILL);

  return -1;
}

extern int asio_addw(struct asio_env * const asio, void * const work)
{
  _asio_enq(asio, work);

  return 0;
}


#ifdef ASIO_MAIN
/*--------------------------------------------------------------------------*/
/*  MAIN                                                                    */
/*--------------------------------------------------------------------------*/
#include <pthread.h>    /* pthread library */
#include <semaphore.h>  /* semaphore library */
#include <signal.h>     /* SIGQUIT */
#include <stdio.h>      /* stderr, fprintf */
#include <stdlib.h>     /* EXIT_SUCCESS, EXIT_FAILURE, abort */
#include <unistd.h>     /* alarm */

#define ASIO_SIZE 32
#define ASIO_NUMT 4

#define ASIO_GET_SEM(SEM)                                                   \
do {                                                                        \
  if (0 != sem_wait(SEM)) {                                                 \
    ASIO_ERROR("Semaphore lock failed [errno: %d %s]\n", errno,             \
      strerror(errno));                                                     \
  }                                                                         \
} while (0)

#define ASIO_LET_SEM(SEM)                                                   \
do {                                                                        \
  if (0 != sem_post(SEM)) {                                                 \
    ASIO_ERROR("Semaphore unlock failed [errno: %d %s]\n", errno,           \
      strerror(errno));                                                     \
  }                                                                         \
} while (0)

#define ASIO_TRY_SEM(SEM, BOOL)                                             \
do {                                                                        \
  if (0 == sem_trywait(SEM)) {                                              \
    (BOOL) = 1;                                                             \
  }                                                                         \
  else if (EAGAIN == errno) {                                               \
    (BOOL) = 0;                                                             \
  }                                                                         \
  else {                                                                    \
    ASIO_ERROR("Semaphore try lock failed [errno: %d %s]\n", errno,         \
      strerror(errno));                                                     \
  }                                                                         \
} while (0)

struct asio_work
{
  int sig;
  sem_t sem;
  pthread_mutex_t mtx;
};

static void cancel(void * const arg)
{
  struct asio_work * const work=(struct asio_work *)arg;

  ASIO_LET_SEM(&(work->sem));
  ASIO_LET_LOCK(&(work->mtx));
}

static void cb(void * const arg)
{
#ifndef NDEBUG
  int sval;
#endif
  struct asio_work * work;
  struct timespec ts;

  if (NULL == arg) {
    fprintf(stderr, "Error: Callback received invalid arg at line %d of "   \
      "file %s.\n", __LINE__, __FILE__);
    abort();
  }
  work = (struct asio_work *)arg;

  pthread_cleanup_push(cancel, work);
  ASIO_GET_LOCK(&(work->mtx));

  assert(0 == sem_getvalue(&(work->sem), &sval));
  assert(0 == sval);

  for (;;) {
    /***************************************************/
    /* Handle any received ``signals'' */
    /***************************************************/
    ASIO_LET_LOCK(&(work->mtx));

    /* Time-out so that other threads can lock mutex and update work->sig if
     * need be. */
    ts.tv_sec  = 0;
    ts.tv_nsec = 5000000;
    nanosleep(&ts, NULL);

    ASIO_GET_LOCK(&(work->mtx));

    switch (work->sig) {
    case 0:
      break;
    case SIGQUIT:
      goto QUIT;
    default:
      fprintf(stderr, "Error: Callback received invalid signal (%d) at "  \
        "line %d of file %s.\n", work->sig, __LINE__, __FILE__);
      abort();
    }

    /***************************************************/
    /* Do some background work */
    /***************************************************/
    if (0) {
    //saddr  = sbchunk->saddr;
    //npages = sbchunk->npages;
    //pflags = sbchunk->pflags;
    //pgsize = sbinfo->pagesize;

    ///* if required, read the data from the file */
    //if (SBCHUNK_ONDISK == (sbchunk->flags&SBCHUNK_ONDISK)) {
    //  if (-1 == (fd=open(sbchunk->fname, O_RDONLY))) {
    //    perror("_sb_chunkload: failed to open file");
    //    exit(EXIT_FAILURE);
    //  }

    //  for (i=0; i<npages; i+=32) {
    //    iend = i+32 < npages ? i+32 : npages;

    //    /***************************************************/
    //    /* read any required pages */
    //    /***************************************************/
    //    MPROTECT(sbchunk->saddr+(i*pgsize), 32*pgsize, PROT_WRITE);

    //    for (ifirst=-1, ip=i; ip<=iend; ip++) {
    //      if (0 == (pflags[ip]&SBCHUNK_READ)  &&
    //          0 == (pflags[ip]&SBCHUNK_WRITE) && /* uneeded? */
    //          SBCHUNK_ONDISK == (pflags[ip]&SBCHUNK_ONDISK))
    //      {
    //        if (-1 == ifirst)
    //          ifirst = ip;
    //      }
    //      else if (-1 != ifirst) {
    //        if (-1 == lseek(fd, ifirst*pgsize, SEEK_SET)) {
    //          perror("_sb_chunkload: failed on lseek");
    //          exit(EXIT_FAILURE);
    //        }

    //        tsize = (ip-ifirst)*pgsize;
    //        buf = (char *)(saddr+(ifirst*pgsize));
    //        do {
    //          if (-1 == (size=libc_read(fd, buf, tsize))) {
    //            perror("_sb_chunkload: failed to read the required data");
    //            exit(EXIT_FAILURE);
    //          }
    //          buf   += size;
    //          tsize -= size;
    //        } while (tsize > 0);

    //        ifirst = -1;
    //      }
    //    }


    //    /***************************************************/
    //    /* give final protection and set appropriate flags */
    //    /***************************************************/
    //    MPROTECT(saddr+(i*pgsize), npages*pgsize, PROT_READ);

    //    for (ip=i; ip<iend; ip++) {
    //      if (SBCHUNK_NONE == (pflags[ip]&SBCHUNK_NONE))
    //        pflags[ip] ^= SBCHUNK_NONE;
    //      else if (SBCHUNK_WRITE == (pflags[ip]&SBCHUNK_WRITE))
    //        MPROTECT(saddr+ip*pgsize, pgsize, PROT_READ|PROT_WRITE);
    //      pflags[ip] |= SBCHUNK_READ;
    //    }

    //    sbchunk->ldpages += (iend-i);
    //  }

    //  if (close(fd) == -1) {
    //    perror("mtio_start: failed to close the fd");
    //    exit(EXIT_FAILURE);
    //  }
    //}
    //else {
    //  MPROTECT(saddr, npages*pgsize, PROT_READ);

    //  for (ip=0; ip<npages; ip++) {
    //    if (SBCHUNK_NONE == (pflags[ip]&SBCHUNK_NONE))
    //      pflags[ip] ^= SBCHUNK_NONE;
    //    else if (SBCHUNK_WRITE == (pflags[ip]&SBCHUNK_WRITE))
    //      MPROTECT(saddr+(ip*pgsize), pgsize, PROT_READ|PROT_WRITE);
    //    pflags[ip] |= SBCHUNK_READ;
    //  }

    //  sbchunk->ldpages = sbchunk->npages;
    //}
    }
  }

QUIT:
  pthread_cleanup_pop(1);
}

static int init_work(struct asio_work * const work)
{
  work->sig = 0;
  if (0 != sem_init(&(work->sem), 0, 1))
    goto CLEANUP;
  if (0 != pthread_mutex_init(&(work->mtx), NULL))
    goto CLEANUP;

  return 0;

CLEANUP:
  return -1;
}

static int make_work(struct asio_env * const asio,
                     struct asio_work * const work)
{
  int hassem=0;

  /* Acquire work lock. */
  ASIO_GET_LOCK(&(work->mtx));

  /* Do some local work. */
  if (0) {
  //SB_SB_IFSET(BDMPI_SB_LAZYWRITE) {
  //  if (sbchunk->flags&SBCHUNK_NONE) {
  //    bdmsg_t msg, gomsg;
  //
  //    /* notify the master that you want to load memory */
  //    memset(&msg, 0, sizeof(bdmsg_t));
  //    msg.msgtype = BDMPI_MSGTYPE_MEMLOAD;
  //    msg.source  = sbinfo->job->rank;
  //    msg.count   = sbchunk->npages*sbinfo->pagesize;
  //    bdmq_send(sbinfo->job->reqMQ, &msg, sizeof(bdmsg_t));
  //    BDMPL_SLEEP(sbinfo->job, gomsg);
  //  }
  //}

  // If page is still not read, read it.
  //if (SBCHUNK_NONE == (sbchunk->pflags[ip]&SBCHUNK_NONE))
  //  _sb_pageload(sbchunk, ip);
  }

  /* Check if chunk is async I/O, 0==hassem:yes, 1==hassem:no. */
  ASIO_TRY_SEM(&(work->sem), hassem);

  /* Release work lock. */
  ASIO_LET_LOCK(&(work->mtx));

  /* If chunk is not already async I/O, make it so. */
  if (1 == hassem) {
    if (0 != asio_addw(asio, work))
      goto CLEANUP;
  }

  return 0;

CLEANUP:
  return -1;
}

static int free_work(struct asio_work * const work)
{
#ifndef NDEBUG
  int sval;
#endif
  int hassem=0;

  /* Check if chunk is async I/O, 0==hassem:yes, 1==hassem:no.  If chunk is
   * async I/O, signal any async threads to quit. */
  ASIO_TRY_SEM(&(work->sem), hassem);
  if (0 == hassem) {
    /* Signal that the async handler of this piece of work, if any, should
     * break. */
    ASIO_GET_LOCK(&(work->mtx));
    assert(0 == work->sig);
    work->sig = SIGQUIT;
    ASIO_LET_LOCK(&(work->mtx));

    /* Wait until async handler has finished */
    ASIO_GET_SEM(&(work->sem));
  }

  /* Free work -- wait until lock is re-acquired to be sure that async thread
   * is finished. */
  ASIO_GET_LOCK(&(work->mtx));
  assert(SIGQUIT == work->sig);
  assert(0 == sem_getvalue(&(work->sem), &sval));
  assert(0 == sval);
  ASIO_LET_LOCK(&(work->mtx));

  if (0 != sem_destroy(&(work->sem)))
    goto CLEANUP;
  if (0 != pthread_mutex_destroy(&(work->mtx)))
    goto CLEANUP;

  return 0;

CLEANUP:
  return -1;
}

int main()
{
  int i;
  struct asio_env asio;
  struct asio_work work[ASIO_SIZE];

  /* Make sure dead-lock does not last indefinitely */
  alarm(1);

  if (0 != asio_init(&asio, ASIO_NUMT, ASIO_SIZE, &cb))
    return EXIT_FAILURE;

  for (i=0; i<ASIO_SIZE; ++i)
    if (0 != init_work(&(work[i])))
      return EXIT_FAILURE;

  for (i=0; i<ASIO_SIZE; ++i)
    if (0 != make_work(&asio, &(work[i])))
      return EXIT_FAILURE;

  for (i=0; i<ASIO_SIZE; ++i)
    if (0 != make_work(&asio, &(work[i])))
      return EXIT_FAILURE;

  for (i=0; i<ASIO_SIZE; ++i)
    if (0 != free_work(&(work[i])))
      return EXIT_FAILURE;

  if (0 != asio_free(&asio))
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}
#endif
