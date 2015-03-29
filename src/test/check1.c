#include <stdio.h>
#include <stdlib.h>

#include "klmalloc.h"

int main(void)
{
  size_t n=2;
  void * arr, * arr2;

  if (NULL == (arr=KL_malloc2(n)))
    return EXIT_FAILURE;
  /*if (NULL == (arr2=KL_malloc2(n)))
    return EXIT_FAILURE;

  KL_free(arr);
  KL_free(arr2);

  if (NULL == (arr=KL_malloc2(n)))
    return EXIT_FAILURE;*/

  KL_free2(arr);

  return EXIT_SUCCESS;
}
