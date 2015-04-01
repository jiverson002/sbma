#define _POSIX_C_SOURCE 200112L

#include <fcntl.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char * argv[])
{
  int fd, num_threads;
  size_t npages, chunk, fsize, size, tsize;
  size_t ip_beg, ip_end, lip_beg, lip_end;
  unsigned long tt;
  struct stat st;
  struct timespec t, ts, te;
  char * buf, * addr, * fname;

  if (3 != argc)
    abort();

  fname       = argv[1];
  num_threads = atoi(argv[2]);

  if (-1 == stat(fname, &st))
    abort();
  fsize = st.st_size;

  if (NULL == (addr=(char*)malloc(fsize)))
    abort();

  ip_beg = 0;
  ip_end = fsize;
  chunk  = 1+(((ip_end-ip_beg)-1)/num_threads);

  clock_gettime(CLOCK_MONOTONIC, &ts);

  #pragma omp parallel default(none)            \
    num_threads(num_threads)                    \
    private(fd,tsize,buf,size,lip_beg,lip_end)  \
    shared(ip_beg,ip_end,addr,fname,chunk)
  {
    /* open the file for reading, and create it if it does not exist */
    if (-1 == (fd=open(fname, O_RDONLY)))
      abort();

    lip_beg = ip_beg+omp_get_thread_num()*chunk;
    lip_end = lip_beg+chunk < ip_end ? lip_beg+chunk : ip_end;

    if (-1 == lseek(fd, lip_beg, SEEK_SET))
      abort();

    buf   = (char*)(addr+lip_beg);
    tsize = lip_end-lip_beg;

    do {
      if (-1 == (size=read(fd, buf, tsize)))
        abort();

      buf   += size;
      tsize -= size;
    } while (tsize > 0);

    /* close file */
    if (-1 == close(fd))
      abort();
  }

  clock_gettime(CLOCK_MONOTONIC, &te);

  if (te.tv_nsec < ts.tv_nsec) {
    t.tv_nsec = 1000000000UL + te.tv_nsec - ts.tv_nsec;
    t.tv_sec = te.tv_sec - 1 - ts.tv_sec;
  }else {
    t.tv_nsec = te.tv_nsec - ts.tv_nsec;
    t.tv_sec = te.tv_sec - ts.tv_sec;
  }
  tt = (unsigned long)(t.tv_sec * 1000000000UL + t.tv_nsec);
  printf("elapsed time: %lu ns\n", tt);

  free(addr);

  return EXIT_SUCCESS;
}
