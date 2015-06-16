#ifndef __KLMALLOC_H__
#define __KLMALLOC_H__ 1


#include <malloc.h> /* struct mallinfo */
#include <stddef.h> /* size_t */


#ifdef __cplusplus
extern "C" {
#endif

enum {
  M_ENABLED,
  M_NUMBER,

    M_ENABLED_ON,
    M_ENABLED_OFF,
    M_ENABLED_PAUSE
};

void * KL_malloc(size_t const size);
void * KL_calloc(size_t const num, size_t const size);
void * KL_realloc(void * const ptr, size_t const size);
int    KL_free(void * const ptr);

int             KL_mallopt(int const param, int const value);
struct mallinfo KL_mallinfo(void);

size_t KL_brick_max_size(void);
size_t KL_chunk_max_size(void);
size_t KL_solo_max_size(void);

#ifdef __cplusplus
}
#endif

#endif
