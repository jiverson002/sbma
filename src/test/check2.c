#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "sbmalloc.h"

int main(void)
{
  size_t i;
  size_t n=100000;
  size_t * arr, * arr2;

  if (0 != SB_fstem("/tmp/xmm-"))
    return EXIT_FAILURE;
  if (0 != SB_mallopt(SBOPT_DEBUG, SBDBG_INFO))
    return EXIT_FAILURE;

  arr = (size_t *)malloc(n*sizeof(size_t));
  if (NULL == arr)
    return EXIT_FAILURE;
  arr2 = (size_t *)malloc(n*sizeof(size_t));
  if (NULL == arr2)
    return EXIT_FAILURE;

  for (i=0; i<n; ++i)
    arr[i] = i;

  msync(arr, n*sizeof(size_t), MS_SYNC);

  for (i=0; i<n; ++i) {
    if (i != arr[i])
      return EXIT_FAILURE;
  }

  free(arr);
  free(arr2);

  arr = (size_t *)malloc(n*sizeof(size_t));
  if (NULL == arr)
    return EXIT_FAILURE;

  for (i=0; i<n; ++i)
    arr[i] = 0;

  free(arr);

  return EXIT_SUCCESS;
}
