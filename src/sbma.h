#ifndef __SBMA_H__
#define __SBMA_H__ 1


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
  MALLOPT_VMMOPTS = 0 /*!< vmm option parameter for mallopt */
};


/****************************************************************************/
/*!
 * Virtual memory manager option bits:
 *
 *   bit 0 ==    0:                      1: lazy read
 */
/****************************************************************************/
enum sbma_vmm_opt_code
{
  VMM_LZYRD = 1 << 0
};

#ifdef __cplusplus
}
#endif


#endif
