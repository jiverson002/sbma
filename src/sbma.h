#ifndef __SBMA_H__
#define __SBMA_H__ 1


#include <sys/types.h> /* ssize_t */


#define SBMA_MAJOR 0
#define SBMA_MINOR 1
#define SBMA_PATCH 0
#define SBMA_RCAND 0


#define SBMA_DEFAULT_PAGE_SIZE (1<<14)
#define SBMA_DEFAULT_FSTEM     "/tmp/"
#define SBMA_DEFAULT_OPTS      0


#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************/
/*! Mallopt parameters. */
/****************************************************************************/
enum sbma_mallopt_params
{
  M_VMMOPTS = 0 /*!< vmm option parameter for mallopt */
};


/****************************************************************************/
/*!
 * Virtual memory manager option bits:
 *
 *   bit 0 ==    0: aggressive read  1: lazy read
 *   bit 1 ==    0: aggressive write 1: lazy write
 */
/****************************************************************************/
enum sbma_vmm_opt_code
{
  VMM_LZYRD = 1 << 0,
  VMM_LZYWR = 1 << 1
};


/****************************************************************************/
/*!
 * Virtual memory manager process eligibility bits:
 *
 *   bit 0 ==    0: ineligible           1: eligible for ipc memory eviction
 */
/****************************************************************************/
enum sbma_ipc_code
{
  IPC_ELIGIBLE = 1 << 0
};


/* malloc.c */
int sbma_init(char const * const fstem, size_t const page_size,
              int const n_procs, size_t const max_mem, int const opts);
int sbma_destroy(void);

void * sbma_malloc(size_t const size);
int    sbma_free(void * const ptr);
void * sbma_realloc(void * const ptr, size_t const size);

/* mstate.c */
ssize_t sbma_mtouch(void * const ptr, size_t const size);
ssize_t sbma_mtouchall(void);
ssize_t sbma_mclear(void * const ptr, size_t const size);
ssize_t sbma_mclearall(void);
ssize_t sbma_mevict(void * const ptr, size_t const size);
ssize_t sbma_mevictall(void);
int     sbma_mexist(void const * const ptr);

/* mextra.c */
int             sbma_mallopt(int const param, int const value);
struct mallinfo sbma_mallinfo(void);
void            sbma_memcpy(void * const dst, void const * const src,
                            size_t const num);
int             sbma_eligible(int const eligible);

#ifdef __cplusplus
}
#endif


#endif
