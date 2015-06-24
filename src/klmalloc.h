/*
Copyright (c) 2015, Jeremy Iverson
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

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
