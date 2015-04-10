#ifndef __KLMALLOC_H__
#define __KLMALLOC_H__ 1


#include <stddef.h> /* size_t */


#ifdef __cplusplus
extern "C" {
#endif

void * libc_malloc(size_t const size);
void * libc_calloc(size_t const num, size_t const size);
void * libc_realloc(void * const ptr, size_t const size);
void   libc_free(void * const ptr);

void * KL_malloc(size_t const size);
void * KL_calloc(size_t const num, size_t const size);
void * KL_realloc(void * const ptr, size_t const size);
void   KL_free(void * const ptr);

void   KL_malloc_stats(void);
void   KL_init(void);
void   KL_finalize(void);

#ifdef __cplusplus
}
#endif

#endif
