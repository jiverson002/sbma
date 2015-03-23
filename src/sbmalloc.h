#ifndef __SBMALLSB_H__
#define __SBMALLSB_H__ 1


#include <stddef.h> /* size_t */


/****************************************************************************/
/* Option constants */
/****************************************************************************/
enum sb_opts
{
  SBOPT_DEBUG,        /* set the debug level */
  SBOPT_MINPAGES,     /* set the minimum allocation size in system pages */
  SBOPT_LAZYREAD,     /* directive to enable lazy reading */

  SBOPT_NUMPAGES,     /* set the number of system pages per sb block */
  SBOPT_MULTITHREAD,  /* directive to enable multi-threaded I/O */
  SBOPT_ENABLED,      /* directive to enable the use of sb* functions */

  SBOPT_NUM
};


/****************************************************************************/
/* Debug level constants */
/****************************************************************************/
enum sb_dbgs
{
  SBDBG_FATAL = 1,  /* show fatal warnings */
  SBDBG_DIAG  = 2,  /* show diagnostic warnings */
  SBDBG_LEAK  = 3,  /* show memory leak information */
  SBDBG_INFO  = 4,  /* show auxiliary information */
  SBDBG_NUM
};


/****************************************************************************/
/*
 *  Page state code bits:
 *
 *    bit 0 ==    0:                     1: synchronized
 *    bit 1 ==    0: clean               1: dirty
 *    bit 2 ==    0:                     1: stored on disk
 */
/****************************************************************************/
enum sb_states
{
  SBPAGE_SYNC   = 1,
  SBPAGE_DIRTY  = 2,
  SBPAGE_ONDISK = 4
};


#ifdef __cplusplus
extern "C" {
#endif

int    sb_mallopt(int const param, int const value);
int    sb_mallget(int const param);
int    sb_fstem(char const * const fstem);
int    sb_acct(void (*acct_charge_cb)(size_t), void (*acct_discharge_cb)(size_t));

int    sb_exists(void const * const addr);

size_t sb_sync(void const * const addr, size_t len);
size_t sb_syncall(void);
size_t sb_load(void const * const addr, size_t len, int const state);
size_t sb_loadall(int const state);
size_t sb_dump(void const * const addr, size_t len);
size_t sb_dumpall(void);

void * sb_malloc(size_t const len);
void * sb_calloc(size_t const num, size_t const size);
void * sb_realloc(void * const addr, size_t const len);
void   sb_free(void * const addr);

#ifdef __cplusplus
}
#endif

#endif
