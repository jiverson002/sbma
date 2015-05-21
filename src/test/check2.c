#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "sbma.h"
#include "klmalloc.h"

int main(void)
{
  size_t i;
  size_t n=100000;
  size_t * arr, * arr2;

  sbma_init(SBMA_DEFAULT_FSTEM, SBMA_DEFAULT_PAGE_SIZE, SBMA_DEFAULT_OPTS);
  KL_mallopt(M_ENABLED, M_ENABLED_ON);

  arr = (size_t *)malloc(n*sizeof(size_t));
  if (NULL == arr)
    return EXIT_FAILURE;
  arr2 = (size_t *)malloc(n*sizeof(size_t));
  if (NULL == arr2)
    return EXIT_FAILURE;

  for (i=0; i<n; ++i)
    arr[i] = i;

  sbma_mevictall();

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

  KL_mallopt(M_ENABLED, M_ENABLED_OFF);
  sbma_destroy();

  return EXIT_SUCCESS;
}
