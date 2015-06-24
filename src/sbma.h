#ifndef __SBMA_H__
#define __SBMA_H__ 1


#include <sys/types.h> /* ssize_t */


#define SBMA_MAJOR 0
#define SBMA_MINOR 1
#define SBMA_PATCH 3
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
enum SBMA_mallopt_params
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
enum SBMA_vmm_opt_code
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
enum SBMA_ipc_code
{
  IPC_ELIGIBLE = 1 << 0
};


/* malloc.c */
void * SBMA_malloc(size_t const size);
void * SBMA_calloc(size_t const num, size_t const size);
int    SBMA_free(void * const ptr);
void * SBMA_realloc(void * const ptr, size_t const size);
int    SBMA_remap(void * const nbase, void * const obase, size_t const size,
                  size_t const off);

/* mcntrl.c */
int SBMA_init(char const * const fstem, size_t const page_size,
              int const n_procs, size_t const max_mem, int const opts);
int SBMA_destroy(void);

/* mextra.c */
int             SBMA_mallopt(int const param, int const value);
struct mallinfo SBMA_mallinfo(void);
int             SBMA_eligible(int const eligible);

/* mstate.c */
ssize_t SBMA_mtouch(void * const ptr, size_t const size);
ssize_t SBMA_mtouchall(void);
ssize_t SBMA_mclear(void * const ptr, size_t const size);
ssize_t SBMA_mclearall(void);
ssize_t SBMA_mevict(void * const ptr, size_t const size);
ssize_t SBMA_mevictall(void);
int     SBMA_mexist(void const * const ptr);

#ifdef __cplusplus
}
#endif


#endif
