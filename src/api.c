#include <malloc.h>    /* struct mallinfo */
#include <stddef.h>    /* size_t */
#include <sys/types.h> /* ssize_t */

#include "klmalloc.h"


/****************************************************************************/
/*! Required function prototypes. */
/****************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

/* malloc.c */
void * __ooc_malloc__(size_t const __size);
void * __ooc_calloc__(size_t const __num, size_t const __size);
void * __ooc_realloc__(void * const __ptr, size_t const __size);
int    __ooc_free__(void * const __ptr);
int    __ooc_remap__(void * const __nptr, void * const __ptr);

/* mcntrl.c */
int    __ooc_init__(char const * const __fstem, size_t const __page_size,
                    int const __n_procs, size_t const __max_mem,
                    int const __opts);
int    __ooc_destroy__(void);

/* mextra.c */
int             __ooc_mallopt__(int const __param, int const __value);
struct mallinfo __ooc_mallinfo__(void);

/* mstate.c */
ssize_t __ooc_mtouch__(void * const __addr, size_t const __len);
ssize_t __ooc_mtouchall__(void);
ssize_t __ooc_mclear__(void * const __addr, size_t const __len);
ssize_t __ooc_mclearall__(void);
ssize_t __ooc_mevict__(void * const __addr, size_t const __len);
ssize_t __ooc_mevictall__(void);
int     __ooc_mexist__(void const * const __addr);
int             __ooc_eligible__(int const __eligible);

#ifdef __cplusplus
}
#endif


/****************************************************************************/
/*! API creator macro. */
/****************************************************************************/
#define API(__PFX, __RETTYPE, __FUNC, __PPARAMS, __PARAMS)\
  extern __RETTYPE sbma_ ## __FUNC __PPARAMS {\
    return __ooc_ ## __FUNC ## __ __PARAMS;\
  }\
  extern __RETTYPE SBMA_ ## __FUNC __PPARAMS {\
    return __PFX ## _ ## __FUNC __PARAMS;\
  }


/****************************************************************************/
/*! API */
/****************************************************************************/
/* malloc.c */
API(KL,   void *, malloc,  (size_t const a), (a))
API(KL,   void *, calloc,  (size_t const a, size_t const b), (a, b))
API(KL,   void *, realloc, (void * const a, size_t const b), (a, b))
API(KL,   int,    free,    (void * const a), (a))
API(sbma, int,    remap,   (void * const a, void * const b), (a, b))

/* mcntrl.c */
extern int
sbma_init(char const * const a, size_t const b, int const c, size_t const d,
          int const e)
{
  return __ooc_init__(a, b, c, d, e);
}
extern int
SBMA_init(char const * const a, size_t const b, int const c, size_t const d,
          int const e)
{
  /* init the sbma subsystem */
  if (-1 == sbma_init(a, b, c, d, e))
    return -1;

  /* enable the klmalloc subsystem */
  if (-1 == KL_mallopt(M_ENABLED, M_ENABLED_ON))
    return -1;

  return 0;
}
extern int
sbma_destroy(void)
{
  return __ooc_destroy__();
}
extern int
SBMA_destroy(void)
{
  /* disable the klmalloc subsystem */
  if (-1 == KL_mallopt(M_ENABLED, M_ENABLED_OFF))
    return -1;

  /* destroy the sbma subsystem */
  if (-1 == sbma_destroy())
    return -1;

  return 0;
}

/* mextra.c */
API(sbma, int,             mallopt, (int const a, int const b), (a, b))
API(sbma, struct mallinfo, mallinfo, (void), ())
API(sbma, int,             eligible, (int const a), (a))

/* mstate.c */
API(sbma, ssize_t, mtouch,    (void * const a, size_t const b), (a, b))
API(sbma, ssize_t, mtouchall, (void), ())
API(sbma, ssize_t, mclear,    (void * const a, size_t const b), (a, b))
API(sbma, ssize_t, mclearall, (void), ())
API(sbma, ssize_t, mevict,    (void * const a, size_t const b), (a, b))
API(sbma, ssize_t, mevictall, (void), ())
API(sbma, int,     mexist,    (void const * const a), (a))
