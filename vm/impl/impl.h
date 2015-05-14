#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#ifdef NDEBUG
# undef NDEBUG
#endif

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#if defined(USE_LOAD)
# include <fcntl.h>
# include <sys/stat.h>
#endif

#if defined(USE_SBMA)
# include <signal.h>
#endif

#if defined(USE_CTX)
# include <sys/ucontext.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(USE_LOAD)
/* io.c */
extern void io_init(char const * const);
extern void io_read(void * const, size_t const, size_t);
extern void io_write(void const * const, size_t);
#endif

/* libc.c */
/* sbma.c */
extern void *       impl_init(size_t const, size_t const);
extern void         impl_destroy(void);
extern char const * impl_name(void);
extern void         impl_flush(void);
extern void         impl_ondisk(void);
extern void         impl_fetch_bulk(void * const, size_t const, size_t const);
extern void         impl_fetch_page(void * const, size_t const, size_t const);

#ifdef __cplusplus
}
#endif
