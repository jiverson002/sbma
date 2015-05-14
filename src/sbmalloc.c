#include "sbconfig.h"

#include <errno.h>        /* errno */
#include <fcntl.h>        /* O_RDWR, O_CREAT, O_EXCL, open, posix_fadvise */
#include <malloc.h>       /* struct mallinfo */
#include <signal.h>       /* struct sigaction, siginfo_t, sigemptyset, sigaction */
#include <stdio.h>        /* stderr, fprintf */
#include <stdlib.h>       /* NULL */
#include <string.h>       /* memset */
#include <sys/mman.h>     /* mmap, munmap, madvise, mprotect */
#include <sys/resource.h> /* rlimit */
#include <sys/stat.h>     /* S_IRUSR, S_IWUSR, open */
#include <sys/time.h>     /* rlimit */
#include <sys/types.h>    /* open */
#include <unistd.h>       /* close, read, write, sysconf */

#include "sbmalloc.h"     /* sbmalloc library */


/*--------------------------------------------------------------------------*/

/* BUGS:
    - This code needs to be updated somehow so that acct(CHARGE) always
      happens before load_range(). Else, data is being loaded before we know
      it can be safely loaded without over-committing memory.
 */

/*--------------------------------------------------------------------------*/


/****************************************************************************/
/* Function prototypes for hooks. */
/****************************************************************************/
extern ssize_t libc_read(int const fd, void * const buf, size_t const count);
extern ssize_t libc_write(int const fd, void const * const buf, size_t const count);
extern int libc_mlock(void const * const addr, size_t const len);
extern int libc_munlock(void const * const addr, size_t const len);


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
  size_t msize;             /* number of bytes mapped */
  size_t app_addr;          /* application handle to the shared mapping */

  size_t len;               /* number of bytes allocated */
  size_t npages;            /* number of pages allocated */
  size_t ld_pages;          /* number of loaded pages */
  size_t ch_pages;          /* number of charged pages */

  char * pflags;            /* per-page flags vector */

#ifdef USE_CHECKSUM
  unsigned int * pchksums;  /* per-page checksums vector */
#endif

  char * fname;             /* the file that will store the data */

  struct sb_alloc * next;   /* singly linked-list of allocations */

#ifdef USE_PTHREAD
  pthread_mutex_t lock;     /* mutex guarding struct */
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
  size_t curpages;          /* current pages loaded */
  size_t numpages;          /* current pages allocated */
  size_t totpages;          /* total pages allocated */
  size_t maxpages;          /* maximum number of pages allocated */

  size_t pagesize;          /* bytes per sbmalloc page */
  size_t minsize;           /* minimum allocation in bytes handled by sbmalloc */

  size_t id;                /* unique id for filenames */
  char fstem[FILENAME_MAX]; /* the file stem where the data is stored */

  struct sb_alloc * head;   /* singly linked-list of allocations */

  struct sigaction act;     /* for the SIGSEGV signal handler */
  struct sigaction oldact;  /* ... */

  int     (*acct_charge_cb)(size_t);  /* function pointers for accounting */
  int     (*acct_discharge_cb)(size_t);                            /* ... */

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


/*--------------------------------------------------------------------------*/


#ifdef USE_CHECKSUM
/****************************************************************************/
/*! Update a crc32 checksum. */
/****************************************************************************/
static unsigned int
sb_internal_crc32_up(unsigned int crc, void const * const buf, size_t size)
{
  static const unsigned int tbl[256] = {
    0x00000000U,0x04C11DB7U,0x09823B6EU,0x0D4326D9U,0x130476DCU,0x17C56B6BU,
    0x1A864DB2U,0x1E475005U,0x2608EDB8U,0x22C9F00FU,0x2F8AD6D6U,0x2B4BCB61U,
    0x350C9B64U,0x31CD86D3U,0x3C8EA00AU,0x384FBDBDU,0x4C11DB70U,0x48D0C6C7U,
    0x4593E01EU,0x4152FDA9U,0x5F15ADACU,0x5BD4B01BU,0x569796C2U,0x52568B75U,
    0x6A1936C8U,0x6ED82B7FU,0x639B0DA6U,0x675A1011U,0x791D4014U,0x7DDC5DA3U,
    0x709F7B7AU,0x745E66CDU,0x9823B6E0U,0x9CE2AB57U,0x91A18D8EU,0x95609039U,
    0x8B27C03CU,0x8FE6DD8BU,0x82A5FB52U,0x8664E6E5U,0xBE2B5B58U,0xBAEA46EFU,
    0xB7A96036U,0xB3687D81U,0xAD2F2D84U,0xA9EE3033U,0xA4AD16EAU,0xA06C0B5DU,
    0xD4326D90U,0xD0F37027U,0xDDB056FEU,0xD9714B49U,0xC7361B4CU,0xC3F706FBU,
    0xCEB42022U,0xCA753D95U,0xF23A8028U,0xF6FB9D9FU,0xFBB8BB46U,0xFF79A6F1U,
    0xE13EF6F4U,0xE5FFEB43U,0xE8BCCD9AU,0xEC7DD02DU,0x34867077U,0x30476DC0U,
    0x3D044B19U,0x39C556AEU,0x278206ABU,0x23431B1CU,0x2E003DC5U,0x2AC12072U,
    0x128E9DCFU,0x164F8078U,0x1B0CA6A1U,0x1FCDBB16U,0x018AEB13U,0x054BF6A4U,
    0x0808D07DU,0x0CC9CDCAU,0x7897AB07U,0x7C56B6B0U,0x71159069U,0x75D48DDEU,
    0x6B93DDDBU,0x6F52C06CU,0x6211E6B5U,0x66D0FB02U,0x5E9F46BFU,0x5A5E5B08U,
    0x571D7DD1U,0x53DC6066U,0x4D9B3063U,0x495A2DD4U,0x44190B0DU,0x40D816BAU,
    0xACA5C697U,0xA864DB20U,0xA527FDF9U,0xA1E6E04EU,0xBFA1B04BU,0xBB60ADFCU,
    0xB6238B25U,0xB2E29692U,0x8AAD2B2FU,0x8E6C3698U,0x832F1041U,0x87EE0DF6U,
    0x99A95DF3U,0x9D684044U,0x902B669DU,0x94EA7B2AU,0xE0B41DE7U,0xE4750050U,
    0xE9362689U,0xEDF73B3EU,0xF3B06B3BU,0xF771768CU,0xFA325055U,0xFEF34DE2U,
    0xC6BCF05FU,0xC27DEDE8U,0xCF3ECB31U,0xCBFFD686U,0xD5B88683U,0xD1799B34U,
    0xDC3ABDEDU,0xD8FBA05AU,0x690CE0EEU,0x6DCDFD59U,0x608EDB80U,0x644FC637U,
    0x7A089632U,0x7EC98B85U,0x738AAD5CU,0x774BB0EBU,0x4F040D56U,0x4BC510E1U,
    0x46863638U,0x42472B8FU,0x5C007B8AU,0x58C1663DU,0x558240E4U,0x51435D53U,
    0x251D3B9EU,0x21DC2629U,0x2C9F00F0U,0x285E1D47U,0x36194D42U,0x32D850F5U,
    0x3F9B762CU,0x3B5A6B9BU,0x0315D626U,0x07D4CB91U,0x0A97ED48U,0x0E56F0FFU,
    0x1011A0FAU,0x14D0BD4DU,0x19939B94U,0x1D528623U,0xF12F560EU,0xF5EE4BB9U,
    0xF8AD6D60U,0xFC6C70D7U,0xE22B20D2U,0xE6EA3D65U,0xEBA91BBCU,0xEF68060BU,
    0xD727BBB6U,0xD3E6A601U,0xDEA580D8U,0xDA649D6FU,0xC423CD6AU,0xC0E2D0DDU,
    0xCDA1F604U,0xC960EBB3U,0xBD3E8D7EU,0xB9FF90C9U,0xB4BCB610U,0xB07DABA7U,
    0xAE3AFBA2U,0xAAFBE615U,0xA7B8C0CCU,0xA379DD7BU,0x9B3660C6U,0x9FF77D71U,
    0x92B45BA8U,0x9675461FU,0x8832161AU,0x8CF30BADU,0x81B02D74U,0x857130C3U,
    0x5D8A9099U,0x594B8D2EU,0x5408ABF7U,0x50C9B640U,0x4E8EE645U,0x4A4FFBF2U,
    0x470CDD2BU,0x43CDC09CU,0x7B827D21U,0x7F436096U,0x7200464FU,0x76C15BF8U,
    0x68860BFDU,0x6C47164AU,0x61043093U,0x65C52D24U,0x119B4BE9U,0x155A565EU,
    0x18197087U,0x1CD86D30U,0x029F3D35U,0x065E2082U,0x0B1D065BU,0x0FDC1BECU,
    0x3793A651U,0x3352BBE6U,0x3E119D3FU,0x3AD08088U,0x2497D08DU,0x2056CD3AU,
    0x2D15EBE3U,0x29D4F654U,0xC5A92679U,0xC1683BCEU,0xCC2B1D17U,0xC8EA00A0U,
    0xD6AD50A5U,0xD26C4D12U,0xDF2F6BCBU,0xDBEE767CU,0xE3A1CBC1U,0xE760D676U,
    0xEA23F0AFU,0xEEE2ED18U,0xF0A5BD1DU,0xF464A0AAU,0xF9278673U,0xFDE69BC4U,
    0x89B8FD09U,0x8D79E0BEU,0x803AC667U,0x84FBDBD0U,0x9ABC8BD5U,0x9E7D9662U,
    0x933EB0BBU,0x97FFAD0CU,0xAFB010B1U,0xAB710D06U,0xA6322BDFU,0xA2F33668U,
    0xBCB4666DU,0xB8757BDAU,0xB5365D03U,0xB1F740B4U
  };
  unsigned char const * p = (unsigned char *)buf;
  while (0 != size--)
    crc = tbl[*p++ ^ ((crc >> 24) & 0xff)] ^ (crc << 8);
  return crc;
}


/****************************************************************************/
/*! Compute crc32 checksum. */
/****************************************************************************/
static unsigned int
sb_internal_crc32(void const * const buf, size_t size)
{
  return sb_internal_crc32_up(0xffffffffU, buf, size) ^ 0xffffffffU;
}
#endif


/*--------------------------------------------------------------------------*/


/****************************************************************************/
/*! Causes process to abnormally exit. */
/****************************************************************************/
static void
sb_internal_abort(char const * const file, int const line, int const flag)
{
  if (1 == flag)
/*#ifdef NDEBUG
    SBWARN(SBDBG_FATAL)("%s", strerror(errno));
  if (NULL == file || 0 == line) {}
#else*/
    SBWARN(SBDBG_FATAL)("%s:%d: %s", basename(file), line, strerror(errno));
//#endif

  kill(getpid(), SIGABRT);
  kill(getpid(), SIGKILL);
  exit(EXIT_FAILURE);
}


/****************************************************************************/
/*! Accounting functionality. */
/****************************************************************************/
static void
sb_internal_acct(int const acct_type, size_t const arg)
{
  int retval=2;

  /* the callback functions must be handled outside of the xm_info.lock
   * section to prevent deadlock. */
  if (SBACCT_CHARGE == acct_type && NULL != sb_info.acct_charge_cb)
    retval = (*sb_info.acct_charge_cb)(arg);
  if (SBACCT_DISCHARGE == acct_type && NULL != sb_info.acct_discharge_cb)
    retval = (*sb_info.acct_discharge_cb)(arg);

  if (retval) {}  /* surpress */

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
    /*if (0 != arg && 0 == retval) {
      fprintf(stderr, "[%5d] here 1 %zu\n", (int)getpid(), arg);
      sb_abort(0);
    }*/
    //if (0 != arg) printf("[%5d] ___charge:    %zu\n", (int)getpid(), arg);
    if (sb_info.curpages > sb_info.maxpages)
      sb_info.maxpages = sb_info.curpages;
    break;

  case SBACCT_DISCHARGE:
    /*if (0 != arg && 0 == retval) {
      fprintf(stderr, "[%5d] here 2 %zu\n", (int)getpid(), arg);
      sb_abort(0);
    }*/
    //if (0 != arg) printf("[%5d] ___discharge: %zu\n", (int)getpid(), arg);
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
#ifdef USE_CHECKSUM
  size_t i;
  unsigned int * pchksums;
#endif
  int fd;
  size_t ip, psize, app_addr, tmp_addr, tsize, off, ip_end, numrd=0;
  size_t chunk, lip_beg, lip_end;
  ssize_t size, ipfirst;
  char * buf, * pflags;

  if (NULL == sb_alloc)
    return 0;
  if (npages > sb_alloc->npages)
    return 0;
  if (ip_beg > sb_alloc->npages-npages)
    return 0;

  psize    = sb_info.pagesize;
  app_addr = sb_alloc->app_addr;
  pflags   = sb_alloc->pflags;
  ip_end   = ip_beg+npages;
#ifdef USE_CHECKSUM
  pchksums = sb_alloc->pchksums;
#endif

  if (SBPAGE_SYNC == state) {
    /* Shortcut by checking to see if all pages are already loaded */
    if (sb_alloc->ld_pages == sb_alloc->npages)
      return 0;

    //SBFADVISE(fd, ip_beg*psize, (ip_end-ip_beg)*psize,
    //  POSIX_FADV_WILLNEED|POSIX_FADV_SEQUENTIAL|POSIX_FADV_NOREUSE);

//#if defined(USE_PTHREAD) && defined(USE_BULK)
//    /* mmap a page into a temporary address with write privileges. */
//# ifdef USE_CHECKSUM
//    SBMMAP(tmp_addr, (ip_end-ip_beg)*psize, PROT_READ|PROT_WRITE);
//# else
//    SBMMAP(tmp_addr, (ip_end-ip_beg)*psize, PROT_WRITE);
//# endif
//#elif !defined(USE_PTHREAD)
# ifdef USE_CHECKSUM
    SBMPROTECT(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize,
      PROT_READ|PROT_WRITE);
# else
    SBMPROTECT(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize, PROT_WRITE);
# endif
    tmp_addr = app_addr;
//#endif

    /* Load only those pages which are on disk and are not already synched
     * with the disk or dirty. */
    /*#pragma omp parallel default(none)                                      \
      if(sb_opts[SBOPT_MULTITHREAD] > 1 &&                               \
         ip_end-ip_beg > (size_t)sb_opts[SBOPT_MULTITHREAD])             \
      num_threads(sb_opts[SBOPT_MULTITHREAD])                            \
      private(i,fd,ip,ipfirst,off,tsize,buf,size,chunk,lip_beg,lip_end)  \
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
//#if defined(USE_PTHREAD) && !defined(USE_BULK)
//# ifdef USE_CHECKSUM
//          SBMMAP(tmp_addr, (ip-ipfirst)*psize, PROT_READ|PROT_WRITE);
//# else
//          SBMMAP(tmp_addr, (ip-ipfirst)*psize, PROT_WRITE);
//# endif
//#endif

//#if defined(USE_PTHREAD) && !defined(USE_BULK)
//          off = 0;
//#elif defined(USE_PTHREAD)
//          off = (ipfirst-ip_beg)*psize;
//#else
          off = ipfirst*psize;
//#endif

          buf   = (char*)(tmp_addr+off);
          tsize = (ip-ipfirst)*psize;

          if (-1 == lseek(fd, ipfirst*psize, SEEK_SET))
            sb_abort(1);

          do {
            if (-1 == (size=libc_read(fd, buf, tsize)))
              sb_abort(1);

            buf   += size;
            tsize -= size;
          } while (tsize > 0);

#ifdef USE_CHECKSUM
          buf = (char*)(tmp_addr+off);
          for (i=0; i<ip-ipfirst; ++i) {
            sb_assert(sb_internal_crc32(buf+i*psize, psize) ==
              pchksums[ipfirst+i]);
          }
#endif

//#if defined(USE_PTHREAD) && !defined(USE_BULK)
//          /* remove write privileges from temporary pages and grant read-only
//           * privileges. */
//          SBMPROTECT(tmp_addr+off, (ip-ipfirst)*psize, PROT_READ);
//          /* mremap temporary pages to the correct location in persistent
//           * memory region. */
//          SBMREMAP(tmp_addr+off, (ip-ipfirst)*psize, app_addr+(ipfirst*psize));
//#endif

          /*#pragma omp critical*/
          numrd += (ip-ipfirst);

          ipfirst = -1;
        }
      }

      /* close file */
      if (-1 == close(fd))
        sb_abort(1);
    }

//#if !defined(USE_PTHREAD)
    /* remove write privileges from pages and grant read-only privileges. */
    SBMPROTECT(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize, PROT_READ);
//# ifdef USE_BULK
//    /* mremap temporary pages to the correct location in persistent memory
//     * region. */
//    SBMREMAP(tmp_addr, (ip_end-ip_beg)*psize, app_addr+(ip_beg*psize));
//# endif
//#endif

    for (ip=ip_beg; ip<ip_end; ++ip) {
      if (SBISSET(pflags[ip], SBPAGE_DUMP)) {
        /* - DUMP flag */
        pflags[ip] &= ~SBPAGE_DUMP;
      }

      /* count unloaded pages */
      if (!SBISSET(pflags[ip], SBPAGE_DIRTY) &&
          !SBISSET(pflags[ip], SBPAGE_SYNC)) {
        sb_assert(sb_alloc->ld_pages < sb_alloc->npages);
        sb_alloc->ld_pages++;

        /* + SYNC flag */
        pflags[ip] |= SBPAGE_SYNC;
      }
//#if !defined(USE_PTHREAD)
      /* leave dirty pages dirty */
      else if (SBISSET(pflags[ip], SBPAGE_DIRTY)) {
        SBMPROTECT(app_addr+(ip*psize), psize, PROT_READ|PROT_WRITE);
      }
//#endif
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

      /* - SYNC/DUMP/ONDISK flag */
      pflags[ip] &= ~(SBPAGE_SYNC|SBPAGE_DUMP|SBPAGE_ONDISK);
      /* + DIRTY flag */
      pflags[ip] |= SBPAGE_DIRTY;
    }

    SBMPROTECT(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize,
      PROT_READ|PROT_WRITE);
  }

  SBMLOCK(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize);

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
#ifdef USE_CHECKSUM
  size_t i;
  unsigned int * pchksums;
#endif
  int fd;
  size_t ip, psize, app_addr, tsize, off, ip_end, num=0;
  size_t chunk, lip_beg, lip_end;
  ssize_t size, ipfirst;
  char * buf, * pflags;

  if (NULL == sb_alloc)
    return 0;
  if (npages > sb_alloc->npages)
    return 0;
  if (ip_beg > sb_alloc->npages-npages)
    return 0;

  psize    = sb_info.pagesize;
  app_addr = sb_alloc->app_addr;
  pflags   = sb_alloc->pflags;
  ip_end   = ip_beg+npages;
#ifdef USE_CHECKSUM
  pchksums = sb_alloc->pchksums;
#endif

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

#ifdef USE_CHECKSUM
        for (i=0; i<ip-ipfirst; ++i) {
          //fprintf(stderr, "w[%5d](%5zu) %p %u\n", (int)getpid(), ipfirst+i,
          //  (void*)(buf+i*psize), sb_internal_crc32(buf+i*psize, psize));
          //fflush(stdout);
          pchksums[ipfirst+i] = sb_internal_crc32(buf+i*psize, psize);
        }
#endif

        /* write the data */
        do {
          if (-1 == (size=libc_write(fd, buf, tsize)))
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
    if (SBISSET(pflags[ip], SBPAGE_DUMP)) {
      /* - DUMP flag */
      pflags[ip] &= ~(SBPAGE_DUMP);
    }

    /* count loaded pages */
    if (SBISSET(pflags[ip], SBPAGE_SYNC) ||
        SBISSET(pflags[ip], SBPAGE_DIRTY)) {
      /* - SYNC/DIRTY flag */
      pflags[ip] &= ~(SBPAGE_SYNC|SBPAGE_DIRTY);

      sb_assert(sb_alloc->ld_pages > 0);
      sb_alloc->ld_pages--;
      sb_assert(sb_alloc->ch_pages > 0);
      sb_alloc->ch_pages--;
    }
  }

  SBMUNLOCK(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize);
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
  if (ip_beg > sb_alloc->npages-npages)
    return 0;

  psize    = sb_info.pagesize;
  app_addr = sb_alloc->app_addr;
  pflags   = sb_alloc->pflags;
  ip_end   = ip_beg+npages;

  SBMUNLOCK(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize);
  SBMADVISE(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize, MADV_DONTNEED);
  SBMPROTECT(app_addr+(ip_beg*psize), (ip_end-ip_beg)*psize, PROT_NONE);

  for (ip=ip_beg; ip<ip_end; ++ip) {
    /* count loaded pages */
    if (SBISSET(pflags[ip], SBPAGE_SYNC) ||
        SBISSET(pflags[ip], SBPAGE_DIRTY))
    {
      sb_assert(sb_alloc->ld_pages > 0);
      sb_alloc->ld_pages--;
      sb_assert(sb_alloc->ch_pages > 0);
      sb_alloc->ch_pages--;
    }

    /* - SYNC/DIRTY/ONDISK flag */
    sb_alloc->pflags[ip] &= ~(SBPAGE_SYNC|SBPAGE_DIRTY|SBPAGE_ONDISK);
    /* + DUMP flag */
    sb_alloc->pflags[ip] |= SBPAGE_DUMP;
  }

  /* convert to system pages */
  return SB_TO_SYS(num, psize);
}


/****************************************************************************/
/*! Counts the number of pages of an allocation which are not in a specific
 * state. */
/****************************************************************************/
static size_t
sb_internal_probe(struct sb_alloc * const sb_alloc, size_t const ip_beg,
                  size_t const npages, size_t const state)
{
  size_t ip, psize, ip_end, num=0;
  char * pflags;

  if (NULL == sb_alloc)
    return 0;
  if (npages > sb_alloc->npages)
    return 0;
  if (ip_beg > sb_alloc->npages-npages)
    return 0;

  psize    = sb_info.pagesize;
  pflags   = sb_alloc->pflags;
  ip_end   = ip_beg+npages;

  /* count pages in state*/
  for (ip=ip_beg; ip<ip_end; ++ip)
    num += !(pflags[ip]&state);

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
  size_t ip, rd_num=0, wr_num=0, ch_pages=0;
  size_t addr=(size_t)si->si_addr;
  struct sb_alloc * sb_alloc=NULL;

  if (SIGSEGV != sig) {
    SBWARN(SBDBG_DIAG)("received incorrect signal (%d)", sig);
    return;
  }

  /* find the sb_alloc */
  if (NULL == (sb_alloc=sb_internal_find(addr))) {
    SBWARN(SBDBG_FATAL)("[%5d] received SIGSEGV on unhandled memory location "
      "(%p)", (int)getpid(), (void*)addr);
    sb_abort(0);
  }

  SB_GET_LOCK(&(sb_alloc->lock));
  ip = (addr-sb_alloc->app_addr)/sb_info.pagesize;

  if (!(SBISSET(sb_alloc->pflags[ip], SBPAGE_SYNC))) {
    if (0 == sb_opts[SBOPT_LAZYREAD]) {
      /* charge any pages which aren't charged */
      ch_pages = sb_alloc->npages-sb_alloc->ch_pages;
    }
    else {
      /* if no pages have been charged for this allocation, then charge the
       * whole allocation */
      if (0 == sb_alloc->ch_pages) {
        sb_assert(0 == sb_alloc->ld_pages);
        ch_pages = sb_alloc->npages;
      }
      /* otherwise, this allocation has already been charged once completely;
       * now we must only charge for pages which have since been dumped */
      else if (SBISSET(sb_alloc->pflags[ip], SBPAGE_DUMP)) {
        sb_assert(sb_alloc->ch_pages < sb_alloc->npages);
        ch_pages = 1;
      }
    }
  }
  else {
    /* by the nature of this signal handler, this offending page is
     * necessarily charged. */
    ch_pages = 0;
  }
  sb_alloc->ch_pages += ch_pages;

  /* convert to system pages */
  ch_pages = SB_TO_SYS(ch_pages, sb_info.pagesize);

  /* charge for the pages to be loaded before actually loading them to ensure
   * there is room in memory */
  //SB_LET_LOCK(&(sb_alloc->lock));
  sb_internal_acct(SBACCT_CHARGE, ch_pages);
  //SB_GET_LOCK(&(sb_alloc->lock));

  if (!(SBISSET(sb_alloc->pflags[ip], SBPAGE_SYNC))) {
    if (0 == sb_opts[SBOPT_LAZYREAD]) {
      rd_num = sb_internal_load_range(sb_alloc, 0, sb_alloc->npages,
        SBPAGE_SYNC);
    }
    else {
      rd_num = sb_internal_load_range(sb_alloc, ip, 1, SBPAGE_SYNC);
    }
  }
  else {
    (void)sb_internal_load_range(sb_alloc, ip, 1, SBPAGE_DIRTY);
  }

  SB_LET_LOCK(&(sb_alloc->lock));

  sb_internal_acct(SBACCT_RDFAULT, rd_num);
  sb_internal_acct(SBACCT_WRFAULT, wr_num);

  if (NULL == ctx) {} /* suppress unused warning */
}


/****************************************************************************/
/*! Shuts down the sbmalloc subsystem. */
/****************************************************************************/
static void
sb_internal_destroy(void)
{
  SB_GET_LOCK(&(sb_info.init_lock));

  if (0 == sb_info.init)
    goto DONE;

  sb_info.init = 0;
  if (-1 == sigaction(SIGSEGV, &(sb_info.oldact), NULL))
    sb_abort(1);
  SB_FREE_LOCK(&(sb_info.lock));

  fprintf(stderr, "numrd=%zu\n", sb_info.numrd);
  fprintf(stderr, "numwr=%zu\n", sb_info.numwr);

  DONE:
  SB_LET_LOCK(&(sb_info.init_lock));
}


/****************************************************************************/
/*! Initializes the sbmalloc subsystem. */
/****************************************************************************/
static void
sb_internal_init(void)
{
  size_t npages, minpages;
  struct rlimit lim;

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

  /* setup the signal handler */
  sb_info.act.sa_flags     = SA_SIGINFO;
  sb_info.act.sa_sigaction = sb_internal_handler;
  if (-1 == sigemptyset(&(sb_info.act.sa_mask)))
    goto CLEANUP;
  if (-1 == sigaction(SIGSEGV, &(sb_info.act), &(sb_info.oldact)))
    goto CLEANUP;

  if (-1 == getrlimit(RLIMIT_MEMLOCK, &lim))
    goto CLEANUP;
  lim.rlim_cur = lim.rlim_max;
  if (-1 == setrlimit(RLIMIT_MEMLOCK, &lim))
    goto CLEANUP;

  /* setup the sb_info mutex */
  SB_INIT_LOCK(&(sb_info.lock));

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
/* Return some memory statistics */
/****************************************************************************/
extern struct mallinfo
SB_mallinfo(void)
{
  struct mallinfo mi;

  mi.smblks  = sb_info.numrf; /* number of read faults */
  mi.ordblks = sb_info.numwf; /* number of write faults */
  mi.hblks   = sb_info.numsf; /* number of segmentation faults */

  mi.usmblks = sb_info.numrd; /* number of pages read from disk */
  mi.fsmblks = sb_info.numwr; /* number of pages wrote to disk */

  mi.uordblks = sb_info.curpages; /* pages loaded at time of call */
  mi.fordblks = sb_info.numpages; /* pages allocated at time of call */
  mi.arena    = sb_info.maxpages; /* maximum concurrent memory allocated */
  mi.keepcost = sb_info.totpages; /* total number of allocated pages */

  return mi;
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
SB_acct(int (*acct_charge_cb)(size_t), int (*acct_discharge_cb)(size_t))
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
    len = sb_alloc->app_addr+sb_alloc->len-(size_t)addr;

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
    sb_assert(0 == sb_alloc->ch_pages);
    SB_LET_LOCK(&(sb_alloc->lock));
  }
  SB_LET_LOCK(&(sb_info.lock));

  /* convert to system pages */
  ld_pages = SB_TO_SYS(ld_pages, sb_info.pagesize);

  sb_internal_acct(SBACCT_WRITE, num);
  sb_internal_acct(SBACCT_DISCHARGE, ld_pages);

  return ld_pages;
}


/****************************************************************************/
/*! Load anonymous mmap from disk.  If addr or addr+len falls within a page,
 *  then that whole page will be loaded. */
/****************************************************************************/
extern size_t
SB_load(void const * const addr, size_t len, int const state)
{
  size_t psize, app_addr, npages, ipfirst, ipend;
  size_t num=0, _num1=0, _num2=0, ld_pages=0, _ld_pages1=0, _ld_pages2=0;
  struct sb_alloc * sb_alloc;

  SB_INIT_CHECK

  if (0 == len)
    return 0;

  if (NULL == (sb_alloc=sb_internal_find((size_t)addr))) {
    //SBWARN(SBDBG_DIAG)("attempt to load an unhandled memory location (%p)",
    //  addr);
    return 0;
  }

  SB_GET_LOCK(&(sb_alloc->lock));

  /* shortcut */
  if (SBPAGE_SYNC == state && sb_alloc->npages == sb_alloc->ld_pages) {
    SB_LET_LOCK(&(sb_alloc->lock));
    return 0;
  }

  if (sb_alloc->len < len)
    len = sb_alloc->app_addr+sb_alloc->len-(size_t)addr;

  psize    = sb_info.pagesize;
  app_addr = sb_alloc->app_addr;
  npages   = sb_alloc->npages;

  /* need to make sure that all bytes are captured, thus ipfirst is a floor
   * operation and ipend is a ceil operation. */
  ipfirst = ((size_t)addr == app_addr) ? 0 : ((size_t)addr-app_addr)/psize;
  ipend   = ((size_t)addr+len == app_addr+sb_alloc->len)
          ? npages
          : 1+(((size_t)addr+len-app_addr-1)/psize);

#if 0
  /* Special handling when pages are being set to dirty state. */
  if (SBPAGE_DIRTY == state) {
    sb_assert((void*)(sb_alloc->app_addr+ipfirst*psize) <= addr);
    sb_assert(addr <= (void*)(sb_alloc->app_addr+(ipfirst+1)*psize));
    /* If first page is shared, then it should first be sync'd to make sure
     * that any data in the shared part is loaded. */
    if ((void*)(sb_alloc->app_addr+ipfirst*psize) != addr) {
      _ld_pages1 = sb_alloc->ld_pages;
      //_num1      = sb_internal_load_range(sb_alloc, ipfirst, 1, SBPAGE_SYNC);
      _ld_pages1 = sb_alloc->ld_pages-_ld_pages1;
    }

    sb_assert((void*)(sb_alloc->app_addr+(ipend-1)*psize) <= addr);
    sb_assert(addr <= (void*)(sb_alloc->app_addr+ipend*psize));
    /* If last page is shared, then it should first be sync'd to make sure
     * that any data in the shared part is loaded. */
    if (sb_alloc->app_addr+ipend*psize != (size_t)addr+len) {
      _ld_pages2 = sb_alloc->ld_pages;
      //_num2      = sb_internal_load_range(sb_alloc, ipend-1, 1, SBPAGE_SYNC);
      _ld_pages2 = sb_alloc->ld_pages-_ld_pages2;
    }
  }
#endif

  if (ipfirst < ipend) {
    _ld_pages1 = sb_internal_probe(sb_alloc, ipfirst, ipend-ipfirst,
      SBPAGE_SYNC|SBPAGE_DIRTY);
    //SB_LET_LOCK(&(sb_alloc->lock));
    sb_internal_acct(SBACCT_CHARGE, _ld_pages1);
    //SB_GET_LOCK(&(sb_alloc->lock));

    ld_pages = sb_alloc->ld_pages;
    num      = sb_internal_load_range(sb_alloc, ipfirst, ipend-ipfirst, state);
    ld_pages = sb_alloc->ld_pages-ld_pages;

    sb_assert(_ld_pages1 == SB_TO_SYS(ld_pages, psize));
    _ld_pages1 = 0;
  }

  num      = num + _num1 + _num2;
  ld_pages = ld_pages + _ld_pages1 + _ld_pages2;

  /* charge */
  sb_alloc->ch_pages += ld_pages;

  /* convert to system pages */
  ld_pages = SB_TO_SYS(ld_pages, psize);

  SB_LET_LOCK(&(sb_alloc->lock));

  sb_internal_acct(SBACCT_READ, num);
  //sb_internal_acct(SBACCT_CHARGE, ld_pages);

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
    sb_alloc->ch_pages -= sb_alloc->npages-sb_alloc->ld_pages;
    sb_assert(sb_alloc->npages == sb_alloc->ch_pages);
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

  return ld_pages;
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
    len = sb_alloc->app_addr+sb_alloc->len-(size_t)addr;

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
    ld_pages = ld_pages-sb_alloc->ld_pages;

    /* convert to system pages */
    ld_pages = SB_TO_SYS(ld_pages, psize);
  }

  SB_LET_LOCK(&(sb_alloc->lock));

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
#ifdef USE_CHECKSUM
  unsigned int * pchksums=NULL;
#endif
  int fd=-1;
  size_t npages, psize, meta_size, msize;
  size_t app_addr=(size_t)MAP_FAILED;
  char * fname=NULL, * pflags=NULL;
  struct sb_alloc * sb_alloc=NULL;

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
  meta_size  = (sizeof(struct sb_alloc))+(npages+1)+(100+strlen(sb_info.fstem));
#ifdef USE_CHECKSUM
  meta_size += (sizeof(unsigned int)*npages);
#endif
  meta_size  = (1+((meta_size-1)/psize))*psize;
  msize      = npages*psize+meta_size;

  /* allocate memory */
  SBMMAP(app_addr, msize, PROT_NONE);

  /* read/write protect internal memory */
  SBMPROTECT(app_addr+npages*psize, meta_size, PROT_READ|PROT_WRITE);

  /* allocate the allocation structure */
  sb_alloc = (struct sb_alloc*)(app_addr+npages*psize);
  /* allocate the per-page flag vector */
  pflags = (char*)((size_t)sb_alloc+sizeof(struct sb_alloc));
  /* create the filename for storage purposes */
  fname = (char*)((size_t)pflags+(npages+1));
#ifdef USE_CHECKSUM
  pchksums = (unsigned int*)((size_t)fname+100+strlen(sb_info.fstem));
#endif

  /* create and truncate the file to size */
  if (0 > sprintf(fname, "%s%d-%p", sb_info.fstem, (int)getpid(),
      (void*)sb_alloc)) {
    SBWARN(SBDBG_DIAG)("%s", strerror(errno));
    goto CLEANUP;
  }
  if (-1 == (fd=open(fname, O_RDWR|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR))) {
    SBWARN(SBDBG_DIAG)("%s", strerror(errno));
    goto CLEANUP;
  }
  if (-1 == ftruncate(fd, npages*psize)) {
    SBWARN(SBDBG_DIAG)("%s", strerror(errno));
    goto CLEANUP;
  }
  if (-1 == close(fd)) {
    SBWARN(SBDBG_DIAG)("%s", strerror(errno));
    goto CLEANUP;
  }
  /* fd = -1; */ /* Only need if a goto CLEANUP follows */

  /* populate sb_alloc structure */
  sb_alloc->msize    = msize;
  sb_alloc->ld_pages = 0;
  sb_alloc->ch_pages = 0;
  sb_alloc->npages   = npages;
  sb_alloc->len      = len;
  sb_alloc->fname    = fname;
  sb_alloc->app_addr = app_addr;
  sb_alloc->pflags   = pflags;
#ifdef USE_CHECKSUM
  sb_alloc->pchksums = pchksums;
#endif

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
  sb_internal_acct(SBACCT_DISCHARGE, SB_TO_SYS(sb_alloc->ch_pages,
    sb_info.pagesize));
  sb_internal_acct(SBACCT_FREE, SB_TO_SYS(sb_alloc->npages,
    sb_info.pagesize));

  /* free resources */
  SB_FREE_LOCK(&(sb_alloc->lock));

  if (-1 == unlink(sb_alloc->fname))
    sb_abort(1);

  SBMUNMAP(sb_alloc->app_addr, sb_alloc->msize);
}


/****************************************************************************/
/*! Frees the memory associated with an anonymous mmap. */
/****************************************************************************/
extern void
SB_init(void)
{
  sb_internal_init();
}


/****************************************************************************/
/*! Frees the memory associated with an anonymous mmap. */
/****************************************************************************/
extern void
SB_finalize(void)
{
  sb_internal_destroy();
}
