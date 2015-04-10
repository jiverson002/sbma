#ifndef __SBMALLOC_H__
#define __SBMALLOC_H__ 1


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
  SBPAGE_DUMP   = 4,
  SBPAGE_ONDISK = 8
};


#ifdef __cplusplus
extern "C" {
#endif

int SB_mallopt(int const param, int const value);
int SB_mallget(int const param);
int SB_fstem(char const * const fstem);
int SB_acct(int (*acct_charge_cb)(size_t), int (*acct_discharge_cb)(size_t));

int SB_exists(void const * const addr);

size_t SB_sync(void const * const addr, size_t len);
size_t SB_syncall(void);
size_t SB_load(void const * const addr, size_t len, int const state);
size_t SB_loadall(int const state);
size_t SB_dump(void const * const addr, size_t len);
size_t SB_dumpall(void);

void * SB_malloc(size_t const len);
void   SB_free(void * const addr);

void SB_init(void);
void SB_destroy(void);

#ifdef __cplusplus
}
#endif

#endif
