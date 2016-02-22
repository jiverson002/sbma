#ifndef __SBMALLOC_H__
#define __SBMALLOC_H__ 1


#include <malloc.h>    /* struct mallinfo */
#include <stddef.h>    /* size_t */
#include <sys/types.h> /* ssize_t */


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


#ifdef __cplusplus
extern "C" {
#endif

void * SB_mmap(size_t const __len);
void   SB_munmap(void * const __addr, size_t const __len);

ssize_t SB_mtouch(void * const __addr, size_t __len);
ssize_t SB_mtouchall(void);
ssize_t SB_mclear(void * const __addr, size_t __len);
ssize_t SB_mclearall(void);
ssize_t SB_mevict(void * const __addr);
ssize_t SB_mevictall(void);
int     SB_mexist(void const * const __addr);

int SB_mopt(int const param, int const value);
int SB_mcal(int (*acct_charge_cb)(size_t), int (*acct_discharge_cb)(size_t));
int SB_mfile(char const * const file);
struct mallinfo SB_minfo(void);

void SB_init(void);
void SB_finalize(void);

#ifdef __cplusplus
}
#endif

#endif
