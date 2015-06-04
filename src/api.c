#include <malloc.h>    /* struct mallinfo */
#include <stddef.h>    /* size_t */
#include <sys/types.h> /* ssize_t */


/****************************************************************************/
/*! Required function prototypes. */
/****************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

/* malloc.c */
int    __ooc_init__(char const * const __fstem, size_t const __page_size,
                    int const __n_procs, size_t const __max_mem,
                    int const __opts);
int    __ooc_destroy__(void);
void * __ooc_malloc__(size_t const __size);
int    __ooc_free__(void * const __ptr);
void * __ooc_realloc__(void * const __ptr, size_t const __size);

/* mstate.c */
ssize_t __ooc_mtouch__(void * const __addr, size_t const __len);
ssize_t __ooc_mtouchall__(void);
ssize_t __ooc_mclear__(void * const __addr, size_t const __len);
ssize_t __ooc_mclearall__(void);
ssize_t __ooc_mevict__(void * const __addr, size_t const __len);
ssize_t __ooc_mevictall__(void);
int     __ooc_mexist__(void const * const __addr);

/* mextra.c */
int             __ooc_mallopt__(int const __param, int const __value);
struct mallinfo __ooc_mallinfo__(void);
int             __ooc_memcpy__(void * __dst, void const * __src,
                               size_t __num);
int             __ooc_eligible__(int const __eligible);

#ifdef __cplusplus
}
#endif


/****************************************************************************/
/*! API creator macro. */
/****************************************************************************/
#define API(__RETTYPE, __FUNC, __PPARAMS, __PARAMS)\
  extern __RETTYPE sbma_ ## __FUNC __PPARAMS {\
    return __ooc_ ## __FUNC ## __ __PARAMS;\
  }


/****************************************************************************/
/*! API */
/****************************************************************************/
/* malloc.c */
API(int, init,       (char const * const a, size_t const b, int const c,\
                      size_t const d, int const e), (a, b, c, d, e))
API(int, destroy,    (void), ())

API(void *, malloc,  (size_t const a), (a))
API(int,    free,    (void * const a), (a))
API(void *, realloc, (void * const a, size_t const b), (a, b))

/* mstate.c */
API(ssize_t, mtouch,    (void * const a, size_t const b), (a, b))
API(ssize_t, mtouchall, (void), ())
API(ssize_t, mclear,    (void * const a, size_t const b), (a, b))
API(ssize_t, mclearall, (void), ())
API(ssize_t, mevict,    (void * const a, size_t const b), (a, b))
API(ssize_t, mevictall, (void), ())
API(int,     mexist,    (void const * const a), (a))

/* mextra.c */
API(int,             mallopt, (int const a, int const b), (a, b))
API(struct mallinfo, mallinfo, (void), ())
API(int,             memcpy, (void * const a, void const * const b,\
                              size_t const c), (a,b,c))
API(int,             eligible, (int const a), (a))
