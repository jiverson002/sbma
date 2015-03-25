#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "sbmalloc.h"

#define MIN(A, B) (((A) < (B)) ? (A) : (B))

static void
mm(size_t const m, size_t const n, size_t const s, size_t const * const a,
   size_t const * const b, size_t * const c)
{
  size_t i, j, k, jj, kk, tmp;

  for (jj=0; jj<m; jj+=s) {
    for (kk=0; kk<n; kk+=s) {
#pragma omp parallel for private (i, j, k, tmp) shared(jj, kk)
      for (i=0; i<m; ++i) {
        for (j=jj; j<MIN(jj+s, m); ++j) {
          for (tmp=0,k=kk; k<MIN(kk+s, n); ++k) {
            tmp += a[i*m+k]*b[k*n+j];
          }
          c[i*m+j] += tmp;
        }
      }
    }
  }
}

int
main()
{
  size_t i, m=2000, n=2000;
  size_t * a, * b, * c;

  if (0 != SB_mallopt(SBOPT_DEBUG, SBDBG_INFO))
    return EXIT_FAILURE;
  if (0 != SB_mallopt(SBOPT_LAZYREAD, 1))
    return EXIT_FAILURE;

  a = (size_t *)malloc(m*n*sizeof(size_t));
  b = (size_t *)malloc(n*m*sizeof(size_t));
  c = (size_t *)calloc(m*m, sizeof(size_t));

  if (NULL == a)
    return EXIT_FAILURE;
  if (NULL == b)
    return EXIT_FAILURE;
  if (NULL == c)
    return EXIT_FAILURE;

  for (i=0; i<m*n; ++i) {
    a[i] = 1;
    b[i] = 1;
  }

  msync(a, m*n*sizeof(size_t), MS_SYNC);

  mm(m, n, 25, a, b, c);

  msync(c, m*m*sizeof(size_t), MS_SYNC);

  for (i=0; i<m*n; ++i) {
    if (2000 != c[i])
      return EXIT_FAILURE;
  }

  free(a);
  free(b);
  free(c);

  return EXIT_SUCCESS;
}
