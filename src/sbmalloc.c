#include "sbconfig.h"

#include <dlfcn.h>     /* dlsym */
#include <errno.h>     /* errno */
#include <fcntl.h>     /* O_RDWR, O_CREAT, O_EXCL, open, posix_fadvise */
#include <signal.h>    /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stdio.h>     /* stderr, fprintf */
#include <stdlib.h>    /* NULL */
#include <string.h>    /* memset */
#include <sys/mman.h>  /* mmap, munmap, madvise, mprotect */
#include <sys/stat.h>  /* S_IRUSR, S_IWUSR, open */
#include <sys/types.h> /* open */
#include <unistd.h>    /* close, read, write, sysconf */

#include "sbmalloc.h"  /* sbmalloc library */


/*--------------------------------------------------------------------------*/


/****************************************************************************/
/* Constants for the accounting function. */
/****************************************************************************/
enum sb_acct_type
{
  SBACCT_READ,
  SBACCT_WRITE,
  SBACCT_WRFAULT,
  SBACCT_RDFAULT,
  SBACCT_ALLOC,
  SBACCT_FREE,
  SBACCT_CHARGE,
  SBACCT_DISCHARGE
};


/*--------------------------------------------------------------------------*/


/****************************************************************************/
/* Stores information associated with an external memory allocation. */
/****************************************************************************/
struct sb_alloc
{
  size_t app_addr;        /* application handle to the shared mapping */

  size_t len;             /* number of bytes allocated */
  size_t npages;          /* number of pages allocated */
  size_t ld_pages;        /* number of loaded pages */

  char * pflags;          /* per-page flags vector */

  char * fname;           /* the file that will store the data */

  struct sb_alloc * next; /* singly linked-list of allocations */

#ifdef USE_PTHREAD
  pthread_mutex_t lock;   /* mutex guarding struct */
#endif
};


/*--------------------------------------------------------------------------*/


/****************************************************************************/
/* Stores information associated with the external memory environment. */
/****************************************************************************/
static struct sb_info
{
  int init;                 /* initialized variable */
  int atexit_registered;    /* only register atexit function once */

  size_t numsf;             /* total number of segfaults */
  size_t numrf;             /* total number of read segfaults */
  size_t numwf;             /* total number of write segfaults */
  size_t numrd;             /* total number of pages read */
  size_t numwr;             /* total number of pages written */
  size_t curpages;          /* current bytes loaded */
  size_t numpages;          /* current bytes allocated */
  size_t totpages;          /* total bytes allocated */
  size_t maxpages;          /* maximum number of bytes loaded */

  size_t pagesize;          /* bytes per sbmalloc page */
  size_t minsize;           /* minimum allocation in bytes handled by sbmalloc */

  size_t id;                /* unique id for filenames */
  char fstem[FILENAME_MAX]; /* the file stem where the data is stored */

  struct sb_alloc * head;   /* singly linked-list of allocations */

  struct sigaction act;     /* for the SIGSEGV signal handler */
  struct sigaction oldact;  /* ... */

  void *  (*malloc)(size_t);      /* function pointers from libc */
  void *  (*calloc)(size_t, size_t);                      /* ... */
  void *  (*realloc)(void*, size_t);                      /* ... */
  void    (*free)(void*);                                 /* ... */
  ssize_t (*read)(int, void*, size_t);                    /* ... */
  ssize_t (*write)(int, const void*, size_t);             /* ... */
  size_t  (*fread)(void*, size_t, size_t, FILE*);         /* ... */
  size_t  (*fwrite)(const void*, size_t, size_t, FILE*);  /* ... */
  int     (*mlock)(const void*, size_t);                  /* ... */
  int     (*munlock)(const void*, size_t);                /* ... */
  int     (*mlockall)(int);                               /* ... */
  int     (*munlockall)(void);                            /* ... */
  int     (*msync)(void*, size_t, size_t);                /* ... */

  void    (*acct_charge_cb)(size_t);  /* function pointers for accounting */
  void    (*acct_discharge_cb)(size_t);                            /* ... */

#ifdef USE_PTHREAD
  pthread_mutex_t init_lock;  /* mutex guarding initialization */
  pthread_mutex_t lock;       /* mutex guarding struct */
#endif
} sb_info = {
#ifdef USE_PTHREAD
  .init_lock = PTHREAD_MUTEX_INITIALIZER,
#endif
  .init              = 0,
  .atexit_registered = 0,
  .fstem             = {'/', 't', 'm', 'p', '/', '\0'},
  .acct_charge_cb    = NULL,
  .acct_discharge_cb = NULL
};


/****************************************************************************/
/* User specified options. */
/****************************************************************************/
static int sb_opts[SBOPT_NUM]=
{
  [SBOPT_ENABLED]     = 1,
  [SBOPT_NUMPAGES]    = 4,
  [SBOPT_MINPAGES]    = 4,
  [SBOPT_DEBUG]       = 0,
  [SBOPT_LAZYREAD]    = 0,
  [SBOPT_MULTITHREAD] = 1,
};


/****************************************************************************/
/* Debug strings. */
/****************************************************************************/
static char sb_dbg_str[SBDBG_NUM][100]=
{
  [SBDBG_FATAL] = "error",
  [SBDBG_DIAG]  = "diagnostic",
  [SBDBG_LEAK]  = "memory",
  [SBDBG_INFO]  = "info"
};


/****************************************************************************/
/* memory and function pointer for sb_internal_calloc */
/****************************************************************************/
static int libc_calloc_init=0;
static void *(*libc_calloc)(size_t, size_t)=NULL;
#ifdef USE_PTHREAD
static char sb_internal_calloc_mem[1024*1024]={0};
static char * sb_internal_calloc_ptr=sb_internal_calloc_mem;
#endif


/*--------------------------------------------------------------------------*/


/****************************************************************************/
/*! Causes process to abnormally exit. */
/****************************************************************************/
static void
sb_internal_abort(char const * const file, int const line, int const flag)
{
  if (1 == flag)
#ifdef NDEBUG
    SBWARN(SBDBG_FATAL)("%s", strerror(errno));
  if (NULL == file || 0 == line) {}
#else
    SBWARN(SBDBG_FATAL)("%s:%d: %s", basename(file), line, strerror(errno));
#endif

  kill(getpid(), SIGABRT);
  kill(getpid(), SIGKILL);
  exit(EXIT_FAILURE);
}


#ifdef USE_PTHREAD
/****************************************************************************/
/*! Temporary calloc function to be used only for dlsym.  After the
 *  libc_calloc is populated using dlsym, this function will never be called
 *  again. */
/****************************************************************************/
static void *
sb_internal_calloc(size_t const num, size_t const size)
{
  char * nptr=NULL;

  if (sb_internal_calloc_ptr+(num*size) <= sb_internal_calloc_mem+(1024*1024)) {
    nptr = sb_internal_calloc_ptr;
    memset(nptr, 0, num*size);
    sb_internal_calloc_ptr += (num*size);
  }

  return nptr;
}
#endif


/****************************************************************************/
/*! Accounting functionality. */
/****************************************************************************/
static void
sb_internal_acct(int const acct_type, size_t const arg)
{
  /* the callback functions must be handled outside of the xm_info.lock
   * section to prevent deadlock. */
  if (SBACCT_CHARGE == acct_type && NULL != sb_info.acct_charge_cb)
    (*sb_info.acct_charge_cb)(arg);
  if (SBACCT_DISCHARGE == acct_type && NULL != sb_info.acct_discharge_cb)
    (*sb_info.acct_discharge_cb)(arg);

  //if (SBDBG_INFO > sb_opts[SBOPT_DEBUG])
  //  return;

  SB_GET_LOCK(&(sb_info.lock));

  switch (acct_type) {
  case SBACCT_READ:
    sb_info.numrd += arg;
    break;

  case SBACCT_WRITE:
    sb_info.numwr += arg;
    break;

  case SBACCT_RDFAULT:
    sb_info.numrd += arg;
    sb_info.numrf++;
    sb_info.numsf++;
    break;

  case SBACCT_WRFAULT:
    sb_info.numwr += arg;
    sb_info.numwf++;
    sb_info.numsf++;
    break;

  case SBACCT_ALLOC:
    sb_info.totpages += arg;
    sb_info.numpages += arg;
    break;

  case SBACCT_FREE:
    sb_info.numpages -= arg;
    break;

  case SBACCT_CHARGE:
    sb_info.curpages += arg;
    if (sb_info.curpages > sb_info.maxpages)
      sb_info.maxpages = sb_info.curpages;
    break;

  case SBACCT_DISCHARGE:
    sb_info.curpages -= arg;
    break;
  }

  SB_LET_LOCK(&(sb_info.lock));
}


/****************************************************************************/
/*! Loads the supplied range of pages in sb_alloc and sets their
 *  protection mode.  If state is SBPAGE_SYNC, then this operation will not
 *  overwrite any dirty pages in the range. */
/****************************************************************************/
static size_t
sb_internal_load_range(struct sb_alloc * const sb_alloc,
                          size_t const ip_beg, size_t const npages,
                          int const state)
{
  int fd;
  size_t ip, psize, app_addr, tsize, off, ip_end, numrd=0;
  size_t chunk, lip_beg, lip_end;
  ssize_t size, ipfirst;
  char * buf, * pflags;

  if (NULL == sb_alloc)
    return 0;
  if (npages > sb_alloc->npages)
    return 0;

  psize    = sb_info.pagesize;
  app_addr = sb_alloc->app_addr;
  pflags   = sb_alloc->pflags;
  ip_end   = ip_beg+npages;

  if (SBPAGE_SYNC == state) {
    /* Shortcut by checking to see if all pages are already loaded */
    if (sb_alloc->ld_pages == sb_alloc->npages)
      return 0;

    //SBFADVISE(fd, ip_beg*psize, (ip_end-ip_beg)*psize,
    //  POSIX_FADV_WILLNEED|POSIX_FADV_SEQUENTIAL|POSIX_FADV_NOREUSE);
    SBMPROTECT(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize, PROT_WRITE);

    /* Load only those pages which are on disk and are not already synched
     * with the disk or dirty. */
    /*#pragma omp parallel default(none)                                      \
      if(sb_opts[SBOPT_MULTITHREAD] > 1 &&                               \
         ip_end-ip_beg > (size_t)sb_opts[SBOPT_MULTITHREAD])             \
      num_threads(sb_opts[SBOPT_MULTITHREAD])                            \
      private(fd,ip,ipfirst,off,tsize,buf,size,chunk,lip_beg,lip_end)       \
      shared(ip_end,pflags,psize,app_addr,sb_info,numrd)*/
    {
      /* open the file for reading, and create it if it does not exist */
      if (-1 == (fd=open(sb_alloc->fname, O_RDONLY, S_IRUSR|S_IWUSR)))
       sb_abort(1);

      chunk   = 1+(((ip_end-ip_beg)-1)/omp_get_num_threads());
      lip_beg = ip_beg+omp_get_thread_num()*chunk;
      lip_end = lip_beg+chunk < ip_end ? lip_beg+chunk : ip_end;

      for (ipfirst=-1,ip=lip_beg; ip<=lip_end; ++ip) {
        if (ip != lip_end &&
            !SBISSET(pflags[ip], SBPAGE_SYNC) &&
            !SBISSET(pflags[ip], SBPAGE_DIRTY) &&
            SBISSET(pflags[ip], SBPAGE_ONDISK))
        {
          if (-1 == ipfirst)
            ipfirst = ip;
        }
        else if (-1 != ipfirst) {
          off   = ipfirst*psize;
          buf   = (char*)(app_addr+off);
          tsize = (ip-ipfirst)*psize;

          if (-1 == lseek(fd, off, SEEK_SET))
            sb_abort(1);

          do {
            if (-1 == (size=(*sb_info.read)(fd, buf, tsize)))
              sb_abort(1);

            buf   += size;
            tsize -= size;
          } while (tsize > 0);

          /*#pragma omp critical*/
          numrd += (ip-ipfirst);

          ipfirst = -1;
        }
      }

      /* close file */
      if (-1 == close(fd))
        sb_abort(1);
    }

    SBMPROTECT(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize, PROT_READ);

    for (ip=ip_beg; ip<ip_end; ++ip) {
      /* count unloaded pages */
      if (!SBISSET(pflags[ip], SBPAGE_DIRTY) &&
          !SBISSET(pflags[ip], SBPAGE_SYNC)) {
        sb_assert(sb_alloc->ld_pages < sb_alloc->npages);
        sb_alloc->ld_pages++;

        /* + SYNC flag */
        pflags[ip] |= SBPAGE_SYNC;
      }
      /* leave dirty pages dirty */
      else if (SBISSET(pflags[ip], SBPAGE_DIRTY)) {
        SBMPROTECT(app_addr+(ip*psize), psize, PROT_READ|PROT_WRITE);
      }
    }

    numrd = SB_TO_SYS(numrd, psize);
  }
  else if (SBPAGE_DIRTY == state) {
    for (ip=ip_beg; ip<ip_end; ++ip) {
      /* count unloaded pages */
      if (!SBISSET(pflags[ip], SBPAGE_SYNC) &&
          !SBISSET(pflags[ip], SBPAGE_DIRTY)) {
        sb_assert(sb_alloc->ld_pages < sb_alloc->npages);
        sb_alloc->ld_pages++;
      }

      /* - SYNC/ONDISK flag */
      pflags[ip] &= ~(SBPAGE_SYNC|SBPAGE_ONDISK);
      /* + DIRTY flag */
      pflags[ip] |= SBPAGE_DIRTY;
    }

    SBMPROTECT(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize, PROT_READ|PROT_WRITE);
  }

  return numrd;
}


/****************************************************************************/
/*! Synchronize file on disk with the supplied range of pages in sb_alloc
 *  and set their protection mode. */
/****************************************************************************/
static size_t
sb_internal_sync_range(struct sb_alloc * const sb_alloc,
                          size_t const ip_beg, size_t const npages)
{
  int fd;
  size_t ip, psize, app_addr, tsize, off, ip_end, num=0;
  size_t chunk, lip_beg, lip_end;
  ssize_t size, ipfirst;
  char * buf, * pflags;

  if (NULL == sb_alloc)
    return 0;
  if (npages > sb_alloc->npages)
    return 0;

  psize    = sb_info.pagesize;
  app_addr = sb_alloc->app_addr;
  pflags   = sb_alloc->pflags;
  ip_end   = ip_beg+npages;

  SBMPROTECT(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize, PROT_READ);

  /* go over the pages and write the ones that have changed.
     perform the writes in contigous chunks of changed pages. */
  /*#pragma omp parallel default(none)                                        \
    if(sb_opts[SBOPT_MULTITHREAD] > 1 &&                                 \
       ip_end-ip_beg > (size_t)sb_opts[SBOPT_MULTITHREAD])               \
    num_threads(sb_opts[SBOPT_MULTITHREAD])                              \
    private(fd,ip,ipfirst,off,tsize,buf,size,chunk,lip_beg,lip_end)         \
    shared(ip_end,pflags,psize,app_addr,sb_info,num,stdout)*/
  {
    /* open the file for writing, and create it if it does not exist */
    if (-1 == (fd=open(sb_alloc->fname, O_WRONLY, S_IRUSR|S_IWUSR)))
      sb_abort(1);

    chunk   = 1+(((ip_end-ip_beg)-1)/omp_get_num_threads());
    lip_beg = ip_beg+omp_get_thread_num()*chunk;
    lip_end = lip_beg+chunk < ip_end ? lip_beg+chunk : ip_end;

    for (ipfirst=-1,ip=lip_beg; ip<=lip_end; ++ip) {
      if (ip != lip_end && SBISSET(pflags[ip], SBPAGE_DIRTY)) {
        pflags[ip] |= SBPAGE_ONDISK;
        if (-1 == ipfirst)
          ipfirst = ip;
      }
      else if (-1 != ipfirst) {
        off   = ipfirst*psize;
        tsize = (ip-ipfirst)*psize;
        buf   = (char *)(app_addr+off);

        /* write from [ipfirst...ip) */
        if (-1 == lseek(fd, off, SEEK_SET))
          sb_abort(1);

        /* write the data */
        do {
          if (-1 == (size=(*sb_info.write)(fd, buf, tsize)))
            sb_abort(1);

          buf   += size;
          tsize -= size;
        } while (tsize > 0);

        /*#pragma omp critical*/
        num += (ip-ipfirst);

        ipfirst = -1;
      }
    }

    /* close file */
    if (-1 == close(fd))
      sb_abort(1);
  }

  for (ip=ip_beg; ip<ip_end; ++ip) {
    /* count loaded pages */
    if (SBISSET(pflags[ip], SBPAGE_SYNC) ||
        SBISSET(pflags[ip], SBPAGE_DIRTY)) {
      /* - SYNC/DIRTY flag */
      pflags[ip] &= ~(SBPAGE_SYNC|SBPAGE_DIRTY);

      sb_assert(sb_alloc->ld_pages > 0);
      sb_alloc->ld_pages--;
    }
  }

  SBMADVISE(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize, MADV_DONTNEED);
  SBMPROTECT(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize, PROT_NONE);

  return SB_TO_SYS(num, psize);
}


/****************************************************************************/
/*! Dump changes to a specified region of memory and treat it as if it was
 *  newly allocated. */
/****************************************************************************/
extern size_t
sb_internal_dump_range(struct sb_alloc * const sb_alloc,
                          size_t const ip_beg, size_t const npages)
{
  size_t ip, psize, app_addr, ip_end, num=0;
  char * pflags;

  if (NULL == sb_alloc)
    return 0;
  if (npages > sb_alloc->npages)
    return 0;

  psize    = sb_info.pagesize;
  app_addr = sb_alloc->app_addr;
  pflags   = sb_alloc->pflags;
  ip_end   = ip_beg+npages;

  SBMADVISE(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize, MADV_DONTNEED);
  SBMPROTECT(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize, PROT_NONE);

  for (ip=ip_beg; ip<ip_end; ++ip) {
    /* count loaded pages */
    if (SBISSET(pflags[ip], SBPAGE_SYNC) ||
        SBISSET(pflags[ip], SBPAGE_DIRTY))
    {
      sb_assert(sb_alloc->ld_pages > 0);
      sb_alloc->ld_pages--;
    }

    /* - SYNC/DIRTY/ONDISK flag */
    sb_alloc->pflags[ip] &= ~(SBPAGE_SYNC|SBPAGE_DIRTY|SBPAGE_ONDISK);
  }

  /* convert to system pages */
  return SB_TO_SYS(num, psize);
}


/****************************************************************************/
/*! Returns a pointer to an sb_alloc that contains the specified
 *  address. */
/****************************************************************************/
static struct sb_alloc *
sb_internal_find(size_t const addr)
{
  size_t len, app_addr;
  struct sb_alloc * sb_alloc;

  SB_GET_LOCK(&(sb_info.lock));
  for (sb_alloc=sb_info.head; NULL!=sb_alloc; sb_alloc=sb_alloc->next) {
    SB_GET_LOCK(&(sb_alloc->lock));
    len      = sb_alloc->len;
    app_addr = sb_alloc->app_addr;
    if (addr >= app_addr && addr < app_addr+len) {
      SB_LET_LOCK(&(sb_alloc->lock));
      break;
    }
    SB_LET_LOCK(&(sb_alloc->lock));
  }
  SB_LET_LOCK(&(sb_info.lock));

  return sb_alloc;
}


/****************************************************************************/
/*! The SIGSEGV handler. */
/****************************************************************************/
static void
sb_internal_handler(int const sig, siginfo_t * const si, void * const ctx)
{
  size_t ip, num=0;
  size_t addr=(size_t)si->si_addr;
  struct sb_alloc * sb_alloc=NULL;

  if (SIGSEGV != sig) {
    SBWARN(SBDBG_DIAG)("received incorrect signal (%d)", sig);
    return;
  }

  /* find the sb_alloc */
  if (NULL == (sb_alloc=sb_internal_find(addr))) {
    SBWARN(SBDBG_FATAL)("received SIGSEGV on unhandled memory location (%p)",
      (void*)addr);
    sb_abort(0);
  }

  SB_GET_LOCK(&(sb_alloc->lock));
  ip = (addr-sb_alloc->app_addr)/sb_info.pagesize;

  if (!(SBISSET(sb_alloc->pflags[ip], SBPAGE_SYNC))) {
    if (0 == sb_opts[SBOPT_LAZYREAD]) {
      sb_internal_acct(SBACCT_CHARGE,
        SB_TO_SYS(sb_alloc->npages-sb_alloc->ld_pages,
        sb_info.pagesize));

      num = sb_internal_load_range(sb_alloc, 0, sb_alloc->npages,
        SBPAGE_SYNC);
    }
    else {
      /* having this means that sb_info.acct_charge_cb is invoked for every
       * read fault. this needs to be fixed. */
      sb_internal_acct(SBACCT_CHARGE, SB_TO_SYS(1, sb_info.pagesize));
      /* temporary fix - not working */
      //if (0 == sb_alloc->ld_pages)
      //  sb_internal_acct(SBACCT_CHARGE, SB_TO_SYS(sb_alloc->npages,
      //    sb_info.pagesize));
      //else if (SBISSET(sb_alloc->pflags[ip], SBPAGE_DUMP))
      //  sb_internal_acct(SBACCT_CHARGE, SB_TO_SYS(1, sb_info.pagesize));

      num = sb_internal_load_range(sb_alloc, ip, 1, SBPAGE_SYNC);
    }

    sb_internal_acct(SBACCT_RDFAULT, num);
  }
  else {
    (void)sb_internal_load_range(sb_alloc, ip, 1, SBPAGE_DIRTY);

    sb_internal_acct(SBACCT_WRFAULT, 0);
  }
  SB_LET_LOCK(&(sb_alloc->lock));

  if (NULL == ctx) {} /* suppress unused warning */
}


/****************************************************************************/
/*! Shuts down the sbmalloc subsystem. */
/****************************************************************************/
static void
sb_internal_free(void)
{
  SB_GET_LOCK(&(sb_info.init_lock));
  if (1 != sb_info.init) {
    SB_LET_LOCK(&(sb_info.init_lock));
    return;
  }
  sb_info.init = 2;
  SB_LET_LOCK(&(sb_info.init_lock));

  if (0 != sb_info.numsf)
    SBWARN(SBDBG_INFO)("received SIGSEGV %zu times (rd=%zu, wr=%zu)",
      sb_info.numsf, sb_info.numrf, sb_info.numwf);

  if (0 != sb_info.numwr)
    SBWARN(SBDBG_INFO)("wrote %zu pages to disk", sb_info.numwr);

  if (0 != sb_info.numrd)
    SBWARN(SBDBG_INFO)("read %zu pages from disk", sb_info.numrd);

  if (0 != sb_info.totpages)
    SBWARN(SBDBG_INFO)("allocated a total of %zu pages", sb_info.totpages);

  if (0 != sb_info.maxpages)
    SBWARN(SBDBG_INFO)("maximum simultaneous resident memory was %zu pages",
      sb_info.maxpages);

  if (0 != sb_info.curpages)
    SBWARN(SBDBG_LEAK)("%zu pages still loaded at exit",
      sb_info.curpages);

  if (0 != sb_info.numpages)
    SBWARN(SBDBG_LEAK)("%zu pages still allocated at exit",
      sb_info.curpages);

  SB_GET_LOCK(&(sb_info.init_lock));
  if (-1 == sigaction(SIGSEGV, &(sb_info.oldact), NULL))
    sb_abort(1);
  SB_FREE_LOCK(&(sb_info.lock));
  sb_info.init = 0;
  SB_LET_LOCK(&(sb_info.init_lock));
}


/****************************************************************************/
/*! Initializes the sbmalloc subsystem. */
/****************************************************************************/
static void
sb_internal_init(void)
{
  size_t npages, minpages;

  SB_GET_LOCK(&(sb_info.init_lock));

  if (1 == sb_info.init)
    goto DONE;

  npages    = sb_opts[SBOPT_NUMPAGES];
  minpages  = sb_opts[SBOPT_MINPAGES];

  sb_info.init     = 1;
  sb_info.id       = 0;
  sb_info.numsf    = 0;
  sb_info.numrf    = 0;
  sb_info.numwf    = 0;
  sb_info.numrd    = 0;
  sb_info.numwr    = 0;
  sb_info.curpages = 0;
  sb_info.numpages = 0;
  sb_info.maxpages = 0;
  sb_info.pagesize = npages*sysconf(_SC_PAGESIZE);
  sb_info.minsize  = minpages*sysconf(_SC_PAGESIZE);
  sb_info.head     = NULL;

  if (NULL == (*((void**)&sb_info.malloc)=dlsym(RTLD_NEXT, "malloc")))
    goto CLEANUP;
  if (NULL == (*((void**)&sb_info.calloc)=dlsym(RTLD_NEXT, "calloc")))
    goto CLEANUP;
  if (NULL == (*((void**)&sb_info.realloc)=dlsym(RTLD_NEXT, "realloc")))
    goto CLEANUP;
  if (NULL == (*((void**)&sb_info.free)=dlsym(RTLD_NEXT, "free")))
    goto CLEANUP;
  if (NULL == (*((void**)&sb_info.msync)=dlsym(RTLD_NEXT, "msync")))
    goto CLEANUP;
  if (NULL == (*((void**)&sb_info.read)=dlsym(RTLD_NEXT, "read")))
    goto CLEANUP;
  if (NULL == (*((void**)&sb_info.write)=dlsym(RTLD_NEXT, "write")))
    goto CLEANUP;
  if (NULL == (*((void**)&sb_info.fread)=dlsym(RTLD_NEXT, "fread")))
    goto CLEANUP;
  if (NULL == (*((void**)&sb_info.fwrite)=dlsym(RTLD_NEXT, "fwrite")))
    goto CLEANUP;
  if (NULL == (*((void**)&sb_info.mlock)=dlsym(RTLD_NEXT, "mlock")))
    goto CLEANUP;
  if (NULL == (*((void**)&sb_info.munlock)=dlsym(RTLD_NEXT, "munlock")))
    goto CLEANUP;
  if (NULL == (*((void**)&sb_info.mlockall)=dlsym(RTLD_NEXT, "mlockall")))
    goto CLEANUP;
  if (NULL == (*((void**)&sb_info.munlockall)=dlsym(RTLD_NEXT, "munlockall")))
    goto CLEANUP;

  /* setup the signal handler */
  sb_info.act.sa_flags     = SA_SIGINFO;
  sb_info.act.sa_sigaction = sb_internal_handler;
  if (-1 == sigemptyset(&(sb_info.act.sa_mask)))
    goto CLEANUP;
  if (-1 == sigaction(SIGSEGV, &(sb_info.act), &(sb_info.oldact)))
    goto CLEANUP;

  /* setup the sb_info mutex */
  SB_INIT_LOCK(&(sb_info.lock));

  /* setup atexit function */
  if (0 == sb_info.atexit_registered && 0 != atexit(&sb_internal_free))
    goto CLEANUP;
  sb_info.atexit_registered = 1;

DONE:
  SB_LET_LOCK(&(sb_info.init_lock));
  return;

CLEANUP:
  sb_abort(1);
}


/*--------------------------------------------------------------------------*/


/****************************************************************************/
/*! Set parameters for the sbmalloc subsystem. */
/****************************************************************************/
extern int
SB_mallopt(int const param, int const value)
{
  if (param >= SBOPT_NUM) {
    SBWARN(SBDBG_DIAG)("param too large");
    return -1;
  }
  /*if (SBOPT_NUMPAGES == param && 0 != sb_opts[SBOPT_ENABLED]) {
    SBWARN(SBDBG_DIAG)("cannot change pagesize after sb has been enabled");
    return -1;
  }*/

  sb_opts[param] = value;

  return 0;
}


/****************************************************************************/
/*! Set parameters for the sbmalloc subsystem. */
/****************************************************************************/
extern int
SB_mallget(int const param)
{
  if (param >= SBOPT_NUM) {
    SBWARN(SBDBG_DIAG)("param too large");
    return -1;
  }

  return sb_opts[param];
}


/****************************************************************************/
/*! Set parameters for the sbmalloc subsystem. */
/****************************************************************************/
extern int
SB_fstem(char const * const fstem)
{
  SB_GET_LOCK(&(sb_info.lock));
  strncpy(sb_info.fstem, fstem, FILENAME_MAX-1);
  sb_info.fstem[FILENAME_MAX-1] = '\0';
  SB_LET_LOCK(&(sb_info.lock));

  return 0;
}


/****************************************************************************/
/*! Set functions for sbmalloc accounting system */
/****************************************************************************/
extern int
SB_acct(void (*acct_charge_cb)(size_t), void (*acct_discharge_cb)(size_t))
{
  SB_GET_LOCK(&(sb_info.lock));
  sb_info.acct_charge_cb    = acct_charge_cb;
  sb_info.acct_discharge_cb = acct_discharge_cb;
  SB_LET_LOCK(&(sb_info.lock));

  return 0;
}


/****************************************************************************/
/*! Check if an allocation was created by the sbmalloc system. */
/****************************************************************************/
extern int
SB_exists(void const * const addr)
{
  SB_INIT_CHECK

  return (NULL != sb_internal_find((size_t)addr));
}


/****************************************************************************/
/*! Synchronize anonymous mmap with disk.  If addr or addr+len falls within a
 *  page, then that whole page will be synchronized. */
/****************************************************************************/
extern size_t
SB_sync(void const * const addr, size_t len)
{
  size_t psize, app_addr, npages, ipfirst, ipend, num, ld_pages;
  struct sb_alloc * sb_alloc;

  SB_INIT_CHECK

  if (0 == len)
    return 0;

  if (NULL == (sb_alloc=sb_internal_find((size_t)addr))) {
    SBWARN(SBDBG_DIAG)("attempt to synchronize an unhandled memory "
      "location (%p)", addr);
    return 0;
  }

  SB_GET_LOCK(&(sb_alloc->lock));

  /* shortcut */
  if (0 == sb_alloc->ld_pages) {
    SB_LET_LOCK(&(sb_alloc->lock));
    return 0;
  }

  if (sb_alloc->len < len)
    len = sb_alloc->len;

  psize    = sb_info.pagesize;
  app_addr = sb_alloc->app_addr;
  npages   = sb_alloc->npages;

  /* need to make sure that all bytes are captured, thus ipfirst is a floor
   * operation and ipend is a ceil operation. */
  ipfirst = ((size_t)addr == app_addr) ? 0 : ((size_t)addr-app_addr)/psize;
  ipend   = ((size_t)addr+len == app_addr+sb_alloc->len)
          ? npages
          : 1+(((size_t)addr+len-app_addr-1)/psize);

  ld_pages = sb_alloc->ld_pages;
  num      = sb_internal_sync_range(sb_alloc, ipfirst, ipend-ipfirst);
  ld_pages = ld_pages-sb_alloc->ld_pages;

  /* convert to system pages */
  ld_pages = SB_TO_SYS(ld_pages, psize);

  SB_LET_LOCK(&(sb_alloc->lock));

  sb_internal_acct(SBACCT_WRITE, num);
  sb_internal_acct(SBACCT_DISCHARGE, ld_pages);

  return ld_pages;
}


/****************************************************************************/
/*! Synchronize all anonymous mmaps with disk. */
/****************************************************************************/
extern size_t
SB_syncall(void)
{
  size_t num=0, ld_pages=0;
  struct sb_alloc * sb_alloc;

  SB_INIT_CHECK

  SB_GET_LOCK(&(sb_info.lock));
  for (sb_alloc=sb_info.head; NULL!=sb_alloc; sb_alloc=sb_alloc->next) {
    SB_GET_LOCK(&(sb_alloc->lock));
    ld_pages += sb_alloc->ld_pages;
    num      += sb_internal_sync_range(sb_alloc, 0, sb_alloc->npages);
    sb_assert(0 == sb_alloc->ld_pages);
    SB_LET_LOCK(&(sb_alloc->lock));
  }
  SB_LET_LOCK(&(sb_info.lock));

  /* convert to system pages */
  ld_pages = SB_TO_SYS(ld_pages, sb_info.pagesize);

  sb_internal_acct(SBACCT_WRITE, num);
  sb_internal_acct(SBACCT_DISCHARGE, ld_pages);

  return num;
}


/****************************************************************************/
/*! Load anonymous mmap from disk.  If addr or addr+len falls within a page,
 *  then that whole page will be loaded. */
/****************************************************************************/
extern size_t
SB_load(void const * const addr, size_t len, int const state)
{
  size_t psize, app_addr, npages, ipfirst, ipend, num, ld_pages;
  struct sb_alloc * sb_alloc;

  SB_INIT_CHECK

  if (0 == len)
    return 0;

  if (NULL == (sb_alloc=sb_internal_find((size_t)addr))) {
    SBWARN(SBDBG_DIAG)("attempt to load an unhandled memory location (%p)",
      addr);
    return 0;
  }

  SB_GET_LOCK(&(sb_alloc->lock));

  /* shortcut */
  if (sb_alloc->npages == sb_alloc->ld_pages) {
    SB_LET_LOCK(&(sb_alloc->lock));
    return 0;
  }

  if (sb_alloc->len < len)
    len = sb_alloc->len;

  psize    = sb_info.pagesize;
  app_addr = sb_alloc->app_addr;
  npages   = sb_alloc->npages;

  /* need to make sure that all bytes are captured, thus ipfirst is a floor
   * operation and ipend is a ceil operation. */
  ipfirst = ((size_t)addr == app_addr) ? 0 : ((size_t)addr-app_addr)/psize;
  ipend   = ((size_t)addr+len == app_addr+sb_alloc->len)
          ? npages
          : 1+(((size_t)addr+len-app_addr-1)/psize);

  ld_pages = sb_alloc->ld_pages;
  num      = sb_internal_load_range(sb_alloc, ipfirst, ipend-ipfirst, state);
  ld_pages = sb_alloc->ld_pages-ld_pages;

  /* convert to system pages */
  ld_pages = SB_TO_SYS(ld_pages, psize);

  SB_LET_LOCK(&(sb_alloc->lock));

  sb_internal_acct(SBACCT_READ, num);
  sb_internal_acct(SBACCT_CHARGE, ld_pages);

  return ld_pages;
}


/****************************************************************************/
/*! Load all anonymous mmaps from disk. */
/****************************************************************************/
extern size_t
SB_loadall(int const state)
{
  size_t num=0, ld_pages=0;
  struct sb_alloc * sb_alloc;

  SB_INIT_CHECK

  SB_GET_LOCK(&(sb_info.lock));
  for (sb_alloc=sb_info.head; NULL!=sb_alloc; sb_alloc=sb_alloc->next) {
    SB_GET_LOCK(&(sb_alloc->lock));
    ld_pages += sb_alloc->npages-sb_alloc->ld_pages;
    num      += sb_internal_load_range(sb_alloc, 0, sb_alloc->npages, state);
    sb_assert(sb_alloc->npages == sb_alloc->ld_pages);
    SB_LET_LOCK(&(sb_alloc->lock));
  }
  SB_LET_LOCK(&(sb_info.lock));

  /* convert to system pages */
  ld_pages = SB_TO_SYS(ld_pages, sb_info.pagesize);

  sb_internal_acct(SBACCT_READ, num);
  sb_internal_acct(SBACCT_CHARGE, ld_pages);

  return num;
}


/****************************************************************************/
/*! Dump changes to a specified region of memory and treat it as if it was
 *  newly allocated.  If addr or addr+len falls within a page, then only the
 *  full pages that are in the range addr..addr+len will be discarded. */
/****************************************************************************/
extern size_t
SB_dump(void const * const addr, size_t len)
{
  size_t psize, app_addr, npages, ipfirst, ipend, ld_pages=0;
  struct sb_alloc * sb_alloc;

  SB_INIT_CHECK

  if (0 == len)
    return 0;

  if (NULL == (sb_alloc=sb_internal_find((size_t)addr))) {
    //SBWARN(SBDBG_DIAG)("attempt to dump an unhandled memory location (%p)",
    //  addr);
    return 0;
  }

  SB_GET_LOCK(&(sb_alloc->lock));

  /* shortcut */
  if (0 == sb_alloc->ld_pages) {
    SB_LET_LOCK(&(sb_alloc->lock));
    return 0;
  }

  if (sb_alloc->len < len)
    len = sb_alloc->len;

  psize    = sb_info.pagesize;
  app_addr = sb_alloc->app_addr;
  npages   = sb_alloc->npages;

  /* can only dump pages fully within range, thus ipfirst is a ceil
   * operation and ipend is a floor operation. */
  ipfirst = ((size_t)addr == app_addr)
          ? 0
          : 1+(((size_t)addr-app_addr-1)/psize);
  ipend   = ((size_t)addr+len == app_addr+sb_alloc->len)
          ? npages
          : ((size_t)addr+len-app_addr)/psize;

  if (ipfirst < ipend) {
    ld_pages = sb_alloc->ld_pages;
    (void)sb_internal_dump_range(sb_alloc, ipfirst, ipend-ipfirst);
    ld_pages = sb_alloc->ld_pages-ld_pages;

    /* convert to system pages */
    ld_pages = SB_TO_SYS(ld_pages, psize);
  }

  SB_LET_LOCK(&(sb_alloc->lock));

  if (ld_pages > 0)
    sb_internal_acct(SBACCT_DISCHARGE, ld_pages);

  return ld_pages;
}


/****************************************************************************/
/*! Dump all anonymous mmaps to disk. */
/****************************************************************************/
extern size_t
SB_dumpall(void)
{
  size_t num=0;
  struct sb_alloc * sb_alloc;

  SB_INIT_CHECK

  SB_GET_LOCK(&(sb_info.lock));
  for (sb_alloc=sb_info.head; NULL!=sb_alloc; sb_alloc=sb_alloc->next) {
    SB_GET_LOCK(&(sb_alloc->lock));
    num += sb_internal_dump_range(sb_alloc, 0, sb_alloc->npages);
    sb_assert(0 == sb_alloc->ld_pages);
    SB_LET_LOCK(&(sb_alloc->lock));
  }
  SB_LET_LOCK(&(sb_info.lock));

  return num;
}


/****************************************************************************/
/*! Allocate memory via anonymous mmap. */
/****************************************************************************/
extern void *
SB_malloc(size_t const len)
{
  int fd=-1;
  size_t npages, psize, ssize, msize=0;
  size_t app_addr=(size_t)MAP_FAILED;
  char * fname=NULL, * pflags=NULL;
  struct sb_alloc * sb_alloc=NULL;

  /**************************************************************************/
  /* special case when call comes from dlsym. */
  /**************************************************************************/
  if (0 == libc_calloc_init) {
    libc_calloc_init = 1;
    *((void **) &libc_calloc) = dlsym(RTLD_NEXT, "calloc");
  }
#ifdef USE_PTHREAD
  /* the linux systems are very sensative and obnoxious when it comes to the
   * useage of dlsym.  it seems that when compiled with -pthread, a version of
   * dlsym which uses calloc is called, but when compiled without, this second
   * case is not necessary. */
  else if (1 == libc_calloc_init) {
    libc_calloc_init = 2;
    return sb_internal_calloc(1, len);
  }
#endif

  SB_INIT_CHECK

  /* shortcut */
  if (0 == len) {
    SBWARN(SBDBG_DIAG)("attempt to allocate 0 bytes");
    return NULL;
  }

  /* get memory info */
  psize  = sb_info.pagesize;
  npages = (len+psize-1)/psize;

  /* compute allocation sizes */
  ssize = sizeof(struct sb_alloc);
  msize = npages*psize;

  /* allocate the allocation structure */
  sb_alloc = (struct sb_alloc *)(*sb_info.malloc)(ssize);
  if (NULL == sb_alloc) {
    SBWARN(SBDBG_DIAG)("%s", strerror(errno));
    goto CLEANUP;
  }
  /* allocate the per-page flag vector */
  pflags = (char *)(*sb_info.calloc)(npages+1, sizeof(char));
  if (NULL == pflags) {
    SBWARN(SBDBG_DIAG)("%s", strerror(errno));
    goto CLEANUP;
  }

  /* create the filename for storage purposes */
  fname = (char *)(*sb_info.malloc)(100+strlen(sb_info.fstem));
  if (NULL == fname) {
    SBWARN(SBDBG_DIAG)("%s", strerror(errno));
    goto CLEANUP;
  }
  if (0 > sprintf(fname, "%s%d-%p", sb_info.fstem, (int)getpid(),
      (void*)sb_alloc)) {
    SBWARN(SBDBG_DIAG)("%s", strerror(errno));
    goto CLEANUP;
  }

  /* create and truncate the file to size */
  if (-1 == (fd=open(fname, O_RDWR|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR))) {
    SBWARN(SBDBG_DIAG)("%s", strerror(errno));
    goto CLEANUP;
  }
  if (-1 == ftruncate(fd, npages*psize)) {
    SBWARN(SBDBG_DIAG)("%s", strerror(errno));
    goto CLEANUP;
  }

  /* allocate memory */
  SBMMAP(app_addr, msize, PROT_NONE, fd);

  /* close file descriptor */
  if (-1 == close(fd)) {
    SBWARN(SBDBG_DIAG)("%s", strerror(errno));
    goto CLEANUP;
  }
  /* fd = -1; */ /* Only need if a goto CLEANUP follows */

  /* populate sb_alloc structure */
  sb_alloc->ld_pages = 0;
  sb_alloc->npages   = npages;
  sb_alloc->len      = len;
  sb_alloc->fname    = fname;
  sb_alloc->app_addr = app_addr;
  sb_alloc->pflags   = pflags;

  /* initialize sb_alloc lock */
  SB_INIT_LOCK(&(sb_alloc->lock));

  /* add to linked-list */
  SB_GET_LOCK(&(sb_info.lock));
  sb_alloc->next = sb_info.head;
  sb_info.head   = sb_alloc;
  SB_LET_LOCK(&(sb_info.lock));

  /* accounting */
  sb_internal_acct(SBACCT_ALLOC, SB_TO_SYS(npages, psize));

  return (void *)sb_alloc->app_addr;

CLEANUP:
  if (NULL != sb_alloc)
    free(sb_alloc);
  if (NULL != pflags)
    free(pflags);
  if (MAP_FAILED != (void*)app_addr)
    SBMUNMAP(app_addr, msize);
  if (-1 != fd)
    (void)close(fd);
  return NULL;
}


/****************************************************************************/
/*! Frees the memory associated with an anonymous mmap. */
/****************************************************************************/
extern void
SB_free(void * const addr)
{
  struct sb_alloc * sb_alloc=NULL, * psb_alloc=NULL;

  SB_INIT_CHECK

#ifdef USE_PTHREAD
  /* skip __calloc allocations */
  if ((char*)addr >= sb_internal_calloc_mem &&
    (char*)addr < sb_internal_calloc_mem+1024*1024) {
    return;
  }
#endif

  SB_GET_LOCK(&(sb_info.lock));
  for (sb_alloc=sb_info.head; NULL!=sb_alloc; sb_alloc=sb_alloc->next) {
    SB_GET_LOCK(&(sb_alloc->lock));
    if (sb_alloc->app_addr == (size_t)addr) {
      SB_LET_LOCK(&(sb_alloc->lock));
      break;
    }
    SB_LET_LOCK(&(sb_alloc->lock));
    psb_alloc = sb_alloc;
  }
  if (NULL == sb_alloc) {
    SBWARN(SBDBG_DIAG)("attempt to free an unhandled memory location (%p)",
      addr);
    sb_abort(0);
  }

  /* update the link-list */
  if (NULL == psb_alloc)
    sb_info.head = sb_alloc->next;
  else
    psb_alloc->next = sb_alloc->next;
  SB_LET_LOCK(&(sb_info.lock));

  /* accounting */
  sb_internal_acct(SBACCT_DISCHARGE, SB_TO_SYS(sb_alloc->ld_pages,
    sb_info.pagesize));
  sb_internal_acct(SBACCT_FREE, SB_TO_SYS(sb_alloc->npages,
    sb_info.pagesize));

  /* free resources */
  SB_FREE_LOCK(&(sb_alloc->lock));

  if (-1 == unlink(sb_alloc->fname))
    sb_abort(1);

  SBMUNMAP(sb_alloc->app_addr, sb_alloc->npages*sb_info.pagesize);

  (*sb_info.free)(sb_alloc->fname);
  (*sb_info.free)(sb_alloc->pflags);
  (*sb_info.free)(sb_alloc);
}
