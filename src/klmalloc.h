#ifndef __KLMALLOC_H__
#define __KLMALLOC_H__ 1


#include <stddef.h> /* size_t */


#ifdef __cplusplus
extern "C" {
#endif

void * KL_malloc(size_t const size);
void * KL_calloc(size_t const num, size_t const size);
void * KL_realloc(void * const ptr, size_t const size);
void   KL_free(void * const ptr);
void   KL_malloc_stats(void);

#ifdef __cplusplus
}
#endif

#endif
