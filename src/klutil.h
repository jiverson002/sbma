#ifndef KLUTIL_H
#define KLUTIL_H

#include <stdio.h>

#include "kl.h"

size_t
kl_ilog2(
  size_t v
);

size_t
kl_pow2up(
  size_t v
);

size_t
kl_multup(
  size_t v,
  size_t m
);

void *
kl_mmap_aligned(
  size_t const sz
);

void
kl_munmap_aligned(
  void * const ptr,
  size_t const sz
);

size_t
kl_vmem();

#endif
