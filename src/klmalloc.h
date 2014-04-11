#ifndef KLMALLOC_H
#define KLMALLOC_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

void * klmalloc(size_t const sz);
void * klcalloc(size_t const num, size_t const size);
void * klrealloc(void * const ptr, size_t const sz);
void   klfree(void * ptr);

/*void * malloc(size_t sz);
void * calloc(size_t num, size_t sz);
void * realloc(void * ptr, size_t sz);
void   free(void * ptr);*/

#ifdef __cplusplus
}
#endif

#endif
