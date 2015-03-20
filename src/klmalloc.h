#ifndef KLMALLOC_H
#define KLMALLOC_H

#include <stddef.h> /* size_t */

struct klmallinfo {
  size_t usmblks;
};

#ifdef __cplusplus
extern "C"
{
#endif

void * klmalloc(size_t const sz);
void * klcalloc(size_t const num, size_t const size);
void * klrealloc(void * const ptr, size_t const sz);
void   klfree(void * ptr);
void   klmalloc_stats(void);

#ifdef __cplusplus
}
#endif

#endif
