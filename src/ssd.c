#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PIO
#define FLUSH

#define ELAPSED(TS,TE,TT)                                                   \
do {                                                                        \
  struct timespec t;                                                        \
  if ((TE).tv_nsec < (TS).tv_nsec) {                                        \
    t.tv_nsec = 1000000000UL + (TE).tv_nsec - (TS).tv_nsec;                 \
    t.tv_sec = (TE).tv_sec - 1 - (TS).tv_sec;                               \
  }                                                                         \
  else {                                                                    \
    t.tv_nsec = (TE).tv_nsec - (TS).tv_nsec;                                \
    t.tv_sec = (TE).tv_sec - (TS).tv_sec;                                   \
  }                                                                         \
  (TT) = (unsigned long)(t.tv_sec*1000000000UL+t.tv_nsec);                  \
} while (0)

static long double
io_op(char const * const fname, int const num_threads, int flag)
{
  int fd;
  size_t chunk, fsize, tsize;
#ifdef PIO
  size_t off;
#endif
  size_t ip_beg, ip_end, lip_beg, lip_end;
  ssize_t size;
  unsigned long tt;
  long double MiB, sec;
  struct stat st;
  struct timespec ts, te;
  char * buf, * addr;

  if (-1 == stat(fname, &st))
    abort();
  fsize = st.st_size;

  if (NULL == (addr=(char*)malloc(fsize)))
    abort();

  clock_gettime(CLOCK_MONOTONIC, &ts);

  ip_beg = 0;
  ip_end = fsize;
  chunk  = 1+(((ip_end-ip_beg)-1)/num_threads);

#ifdef PIO
  /* open the file for I/O */
  if (-1 == (fd=open(fname, flag)))
    abort();
#ifdef FLUSH
  /* try and flush OS file cache */
  if (-1 == posix_fadvise(fd, 0, fsize, POSIX_FADV_DONTNEED|POSIX_FADV_NOREUSE))
    abort();
#endif

  #pragma omp parallel default(none)            \
    num_threads(num_threads)                    \
    private(tsize,buf,size,lip_beg,lip_end,off) \
    shared(fd,ip_beg,ip_end,addr,chunk,flag)
#else
  #pragma omp parallel default(none)            \
    num_threads(num_threads)                    \
    private(fd,tsize,buf,size,lip_beg,lip_end)  \
    shared(ip_beg,ip_end,addr,chunk,flag)
#endif
  {
    lip_beg = ip_beg+omp_get_thread_num()*chunk;
    lip_end = lip_beg+chunk < ip_end ? lip_beg+chunk : ip_end;
    buf     = (char*)(addr+lip_beg);
    tsize   = lip_end-lip_beg;

#ifndef PIO
    /* open the file for reading */
    if (-1 == (fd=open(fname, flag)))
      abort();
    /* try and flush OS file cache */
    if (-1 == posix_fadvise(fd, 0, fsize, POSIX_FADV_DONTNEED))
      abort();

    /* seek to correct position in file */
    if (-1 == lseek(fd, lip_beg, SEEK_SET))
      abort();
#else
    off = lip_beg;
#endif

    do {
#ifdef PIO
      if (O_RDONLY == flag) {
        if (-1 == (size=pread(fd, buf, tsize, off)))
          abort();
      }
      else {
        if (-1 == (size=pwrite(fd, buf, tsize, off)))
          abort();
      }
      off += size;
#else
      if (O_RDONLY == flag) {
        if (-1 == (size=read(fd, buf, tsize)))
          abort();
      }
      else {
        if (-1 == (size=write(fd, buf, tsize)))
          abort();
      }
#endif

      buf   += size;
      tsize -= size;
    } while (tsize > 0);

#ifndef PIO
    /* try and flush OS file cache */
    if (-1 == posix_fadvise(fd, 0, fsize, POSIX_FADV_DONTNEED))
      abort();
    /* close file */
    if (-1 == close(fd))
      abort();
#endif
  }

#ifdef PIO
#ifdef FLUSH
  /* try and flush OS file cache */
  if (-1 == posix_fadvise(fd, 0, fsize, POSIX_FADV_DONTNEED))
    abort();
#endif
  /* close file */
  if (-1 == close(fd))
    abort();
#endif
  clock_gettime(CLOCK_MONOTONIC, &te);
  ELAPSED(ts,te,tt);

  free(addr);

  MiB = fsize/1000000.0;
  sec = tt/1000000000.0;

  return MiB/sec;
}

int main(int argc, char * argv[])
{
  long double rd, wr;

  if (3 != argc)
    abort();

  rd = io_op(argv[1], atoi(argv[2]), O_RDONLY);
  printf("%.5Lf,", rd);
  wr = io_op(argv[1], atoi(argv[2]), O_WRONLY);
  printf("%.5Lf\n", wr);

  return EXIT_SUCCESS;
}
