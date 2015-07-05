/*!
 *  \file     asio.h
 *  \brief    Implements an asynchronous work queue, including worker threads
 *            to complete the work in the background.
 *  \date     Started 01/12/2015
 *  \author   Jeremy Iverson
 */


#ifndef __ASIO_H__
#define __ASIO_H__


/*--------------------------------------------------------------------------*/
/*  INCLUDES                                                                */
/*--------------------------------------------------------------------------*/
#include <pthread.h>  /* pthread library */


/*--------------------------------------------------------------------------*/
/*  STRUCTS                                                                 */
/*--------------------------------------------------------------------------*/
/****************************************************************************/
/*! Asynchronous I/O environment */
/****************************************************************************/
struct asio_env
{
  unsigned int nt;        /*!< number of I/O threads */
  pthread_t * threads;    /*!< I/O threads */

  unsigned int sz;        /*!< maximum size of q */
  unsigned int n;         /*!< current size of q */
  unsigned int hd;        /*!< head index of q */
  unsigned int tl;        /*!< tail index of q */
  void ** q;              /*!< work queue */

  void (*cb)(void *);     /*!< work callback function */

  pthread_barrier_t bar;  /*!< initialization barrier */

  pthread_cond_t rd;      /*!< ok read condition */
  pthread_cond_t wr;      /*!< ok write condition */
  pthread_mutex_t mtx;    /*!< mutex guarding updates to q */
};


/*--------------------------------------------------------------------------*/
/*  FUNCTION DECS                                                           */
/*--------------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************/
/*! Initialize asynchronous I/O environment */
/****************************************************************************/
int
asio_init(
  struct asio_env * const asio, /*!< Asynchronous I/O environment */
  unsigned int const size,      /*!< Maximum size of work queue */
  unsigned int const nthreads,  /*!< Number of async I/O threads */
  void (*cb)(void *)            /*!< Dequeue call-back function */
);

/****************************************************************************/
/*! Destroy asynchronous I/O environment */
/****************************************************************************/
int
asio_free(
  struct asio_env * const asio  /*!< Asynchronous I/O environment */
);

/****************************************************************************/
/*! Add work to an asynchronous I/O environment */
/****************************************************************************/
int
asio_addw(
  struct asio_env * const asio, /*!< Asynchronous I/O environment */
  void * const work             /*!< Pointer to work to be completed */
);

#ifdef __cplusplus
}
#endif


#endif
