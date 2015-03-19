/*
                An implementation of dynamic memory allocator
                     J. Iverson <jiverson@cs.umn.edu>
                       Thu Mar 12 14:17:25 CDT 2015

  ...
*/

/*****************************************************************************
From Wikipedia
While the x86 architecture originally did not require aligned memory
access and still works without it, SSE2 instructions on x86 CPUs do
require the data to be 128-bit (16-byte) aligned and there can be
substantial performance advantages from using aligned data on these
architectures. However, there are also instructions for unaligned
access such as MOVDQU.

From C Standard
4.10.3 Memory management functions

   The order and contiguity of storage allocated by successive calls
to the calloc , malloc , and realloc functions is unspecified.  The
pointer returned if the allocation succeeds is suitably aligned so
that it may be assigned to a pointer to any type of object and then
used to access such an object in the space allocated (until the space
is explicitly freed or reallocated).  Each such allocation shall yield
a pointer to an object disjoint from any other object.  The pointer
returned points to the start (lowest byte address) of the allocated
space.  If the space cannot be allocated, a null pointer is returned.
If the size of the space requested is zero, the behavior is
implementation-defined; the value returned shall be either a null
pointer or a unique pointer.  The value of a pointer that refers to
freed space is indeterminate.


4.10.3.1 The calloc function

Synopsis

         #include <stdlib.h>
         void *calloc(size_t nmemb, size_t size);

Description

   The calloc function allocates space for an array of nmemb objects,
each of whose size is size .  The space is initialized to all bits
zero.

Returns

   The calloc function returns either a null pointer or a pointer to
the allocated space.


4.10.3.2 The free function


Synopsis

         #include <stdlib.h>
         void free(void *ptr);

Description

   The free function causes the space pointed to by ptr to be
deallocated, that is, made available for further allocation.  If ptr
is a null pointer, no action occurs.  Otherwise, if the argument does
not match a pointer earlier returned by the calloc , malloc , or
realloc function, or if the space has been deallocated by a call to
free or realloc , the behavior is undefined.

Returns

   The free function returns no value.


4.10.3.3 The malloc function

Synopsis

         #include <stdlib.h>
         void *malloc(size_t size);

Description

   The malloc function allocates space for an object whose size is
specified by size and whose value is indeterminate.

Returns

   The malloc function returns either a null pointer or a pointer to
the allocated space.


4.10.3.4 The realloc function

Synopsis

         #include <stdlib.h>
         void *realloc(void *ptr, size_t size);

Description

   The realloc function changes the size of the object pointed to by
ptr to the size specified by size .  The contents of the object shall
be unchanged up to the lesser of the new and old sizes.  If the new
size is larger, the value of the newly allocated portion of the object
is indeterminate.  If ptr is a null pointer, the realloc function
behaves like the malloc function for the specified size.  Otherwise,
if ptr does not match a pointer earlier returned by the calloc ,
malloc , or realloc function, or if the space has been deallocated by
a call to the free or realloc function, the behavior is undefined.  If
the space cannot be allocated, the object pointed to by ptr is
unchanged.  If size is zero and ptr is not a null pointer, the object
it points to is freed.

Returns

   The realloc function returns either a null pointer or a pointer to
the possibly moved allocated space.
*****************************************************************************/


#ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200112L
#elif _POSIX_C_SOURCE < 200112L
# undef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200112L
#endif
#include <stdlib.h> /* posix_memalign, size_t */
#undef _POSIX_C_SOURCE

#include <assert.h>   /* assert */
#include <limits.h>   /* CHAR_BIT */
#include <stdint.h>   /* uint*_t */
#include <string.h>   /* memset */

/* This alignment should be a power of 2. */
#ifdef MEMORY_ALLOCATION_ALIGNMENT
# define KL_MEMORY_ALLOCATION_ALIGNMENT MEMORY_ALLOCATION_ALIGNMENT
#else
# define KL_MEMORY_ALLOCATION_ALIGNMENT 16
#endif

#ifndef KL_EXPORT
# define KL_EXPORT extern
#endif


#define KL_DEBUG 0
#if defined(KL_DEBUG) && KL_DEBUG > 0
# include <stdio.h>
# define KL_PRINT(...) printf(__VA_ARGS__); fflush(stdout);
#else
# define KL_PRINT(...)
#endif


/****************************************************************************/
/* Relevant type shortcuts */
/****************************************************************************/
typedef uintptr_t uptr;


/****************************************************************************/
/*
  Memory block:

    | size_t | size_t | `memory chunks' |
    +--------+--------+-----------------+

    Memory blocks must be allocated so that their starting address is aligned
    to their size, which is a power of 2.  This allows for a memory chunk
    carved out of a block to be traced back to its containing block by masking
    away all bits according to the block size.


  Memory chunk:

      ACTIVE  | size_t | `used memory' |
              +--------+---------------+

    INACTIVE  | size_t | void * | void * | `unused memory' |
              +--------+--------+--------+-----------------+

    When a memory chunk is inactive, then the two `void *' shall be used for
    the pointers in the doubly-linked list in the free chunk data structure.


  Abbriviations:

    A = active chunk
    B = block
    C = chunk
    F = footer size
    I = inactive chunk
    N = number
    P = pointer ([active=memory], [inactive=doubly-linked list])
    S = size
    T = split chunk
 */
/****************************************************************************/
#define KL_B2S(B)    (*((size_t*)((uptr)(B))))
#define KL_B2N(B)    (*((size_t*)((uptr)(B)+sizeof(size_t))))
#define KL_B2C(B)    (void*)((uptr)(B)+KL_BLOCK_HEADER_SIZE_ALIGNED)

#define KL_C2B(C)    (void*)((uptr)(C)&(~(KL_BLOCK_SIZE_ALIGNED-1)))
#define KL_C2S(C)    (*((size_t*)((uptr)(C))))
#define KL_C2P(C)    (void*)((uptr)(C)+KL_CHUNK_HEADER_SIZE_ALIGNED)
#define KL_C2D(C)    (kl_bin_node_t*)KL_C2P(C)

#define KL_C2T(C)    (void*)((uptr)(C)+KL_C2S(C))
#define KL_C2F(C)    (*(size_t*)((uptr)KL_C2T(C)-sizeof(size_t)))
#define KL_CS2T(C,S) (void*)((uptr)(C)+KL_CHUNK_SIZE_ALIGNED(S))

#define KL_P2C(P)    (void*)((uptr)(P)-KL_CHUNK_HEADER_SIZE_ALIGNED)
#define KL_P2B(P)    KL_C2B(KL_P2C(P))
#define KL_P2S(P)    KL_C2S(KL_P2C(P))
#define KL_P2D(P)    KL_C2D(KL_P2C(P))

#define KL_T2F(T)    (*(size_t*)((uptr)(T)-sizeof(size_t)))
#define KL_T2C(T)    (void*)((uptr)(T)-KL_T2F(T))


/****************************************************************************/
/* Macros to test the status of memory chunks */
/****************************************************************************/
#define KL_ISFIRST(C) ((C) == KL_B2C(KL_C2B(C)))
#define KL_ISLAST(C)  ((uptr)(C)+KL_C2S(C) == (uptr)KL_C2B(C)+KL_B2S(KL_C2B(C)))
#define KL_INUSE(C)   (0 == KL_C2F(C))


/****************************************************************************/
/* Align an unsigned integer value to a power supplied power of 2 */
/****************************************************************************/
#define KL_CHUNK_MIN_ALIGNED                                                \
  KL_SIZE_ALIGNED(                                                          \
    KL_CHUNK_HEADER_SIZE_ALIGNED+2*sizeof(void*)+sizeof(size_t),            \
    KL_MEMORY_ALLOCATION_ALIGNMENT                                          \
  )

#define KL_BLOCK_SIZE_ALIGNED 65536

#define KL_BLOCK_HEADER_SIZE_ALIGNED \
  KL_SIZE_ALIGNED(2*sizeof(size_t), KL_MEMORY_ALLOCATION_ALIGNMENT)

#define KL_CHUNK_HEADER_SIZE_ALIGNED \
  KL_SIZE_ALIGNED(sizeof(size_t), KL_MEMORY_ALLOCATION_ALIGNMENT)

#define KL_SIZE_ALIGNED(S,A) \
  (assert(0 == ((A)&((A)-1))), (((S)+((A)-1))&(~(((A)-1)))))

#define KL_CHUNK_SIZE(S)                                                    \
  (                                                                         \
    assert(0 != (S)),                                                       \
    (1==(S))                                                                \
      ? KL_SIZE_ALIGNED(S, log2size[0])                                     \
      : (KLLOG2((S)-1)<20)                                                  \
        ? KL_SIZE_ALIGNED(S, log2size[KLLOG2((S)-1)])                       \
        : (S)                                                               \
  )

#define KL_CHUNK_SIZE_ALIGNED(S)                                            \
  (                                                                         \
    assert(KL_CHUNK_SIZE(S) >= (S)),                                        \
    KL_SIZE_ALIGNED(                                                        \
      KL_CHUNK_HEADER_SIZE_ALIGNED+sizeof(size_t)+KL_CHUNK_SIZE(S),         \
      KL_MEMORY_ALLOCATION_ALIGNMENT                                        \
    )                                                                       \
  )


/****************************************************************************/
/* Base 2 integer logarithm */
/****************************************************************************/
#if !defined(__INTEL_COMPILER) && !defined(__GNUC__)
  static int kl_builtin_clzl(size_t v) {
    /* result will be nonsense if v is 0 */
    int i;
    for (i=sizeof(size_t)*CHAR_BIT-1; i>=0; --i) {
      if (v & ((size_t)1 << i))
        break;
    }
    return sizeof(size_t)*CHAR_BIT-i-1;
  }
  #define kl_clz(V) kl_builtin_clzl(V)
#else
  #define kl_clz(V) __builtin_clzl(V)
#endif

#define KLLOG2(V) (sizeof(size_t)*CHAR_BIT-1-kl_clz(V))


/****************************************************************************/
/* Lookup tables to convert between size and bin number */
/****************************************************************************/
#define KLNUMBIN   1576
#define KLSMALLBIN 1532

static size_t log2size[64]=
{
  8, 8, 8, 8, 8, 8, 16, 16, 32, 32, 64, 64, 128, 128, 256, 256, 512, 512,
  1024, 1024, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static size_t log2off[64]=
{
  0, 0, 0, 0, 0, 0, 8, 8, 20, 20, 44, 44, 92, 92, 188, 188, 380, 380, 764,
  764, 1532, 1533, 1534, 1535, 1536, 1537, 1538, 1539, 1540, 1541, 1542, 1543,
  1544, 1545, 1546, 1547, 1548, 1549, 1550, 1551, 1552, 1553, 1554, 1555,
  1556, 1557, 1558, 1559, 1560, 1561, 1562, 1563, 1564, 1565, 1566, 1567,
  1568, 1569, 1570, 1571, 1572, 1573, 1574, 1575
};

static size_t bin2size[KLSMALLBIN]=
{
  8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208,
  224, 240, 256, 288, 320, 352, 384, 416, 448, 480, 512, 544, 576, 608, 640,
  672, 704, 736, 768, 800, 832, 864, 896, 928, 960, 992, 1024, 1088, 1152, 1216,
  1280, 1344, 1408, 1472, 1536, 1600, 1664, 1728, 1792, 1856, 1920, 1984, 2048,
  2112, 2176, 2240, 2304, 2368, 2432, 2496, 2560, 2624, 2688, 2752, 2816, 2880,
  2944, 3008, 3072, 3136, 3200, 3264, 3328, 3392, 3456, 3520, 3584, 3648, 3712,
  3776, 3840, 3904, 3968, 4032, 4096, 4224, 4352, 4480, 4608, 4736, 4864, 4992,
  5120, 5248, 5376, 5504, 5632, 5760, 5888, 6016, 6144, 6272, 6400, 6528, 6656,
  6784, 6912, 7040, 7168, 7296, 7424, 7552, 7680, 7808, 7936, 8064, 8192, 8320,
  8448, 8576, 8704, 8832, 8960, 9088, 9216, 9344, 9472, 9600, 9728, 9856, 9984,
  10112, 10240, 10368, 10496, 10624, 10752, 10880, 11008, 11136, 11264, 11392,
  11520, 11648, 11776, 11904, 12032, 12160, 12288, 12416, 12544, 12672, 12800,
  12928, 13056, 13184, 13312, 13440, 13568, 13696, 13824, 13952, 14080, 14208,
  14336, 14464, 14592, 14720, 14848, 14976, 15104, 15232, 15360, 15488, 15616,
  15744, 15872, 16000, 16128, 16256, 16384, 16640, 16896, 17152, 17408, 17664,
  17920, 18176, 18432, 18688, 18944, 19200, 19456, 19712, 19968, 20224, 20480,
  20736, 20992, 21248, 21504, 21760, 22016, 22272, 22528, 22784, 23040, 23296,
  23552, 23808, 24064, 24320, 24576, 24832, 25088, 25344, 25600, 25856, 26112,
  26368, 26624, 26880, 27136, 27392, 27648, 27904, 28160, 28416, 28672, 28928,
  29184, 29440, 29696, 29952, 30208, 30464, 30720, 30976, 31232, 31488, 31744,
  32000, 32256, 32512, 32768, 33024, 33280, 33536, 33792, 34048, 34304, 34560,
  34816, 35072, 35328, 35584, 35840, 36096, 36352, 36608, 36864, 37120, 37376,
  37632, 37888, 38144, 38400, 38656, 38912, 39168, 39424, 39680, 39936, 40192,
  40448, 40704, 40960, 41216, 41472, 41728, 41984, 42240, 42496, 42752, 43008,
  43264, 43520, 43776, 44032, 44288, 44544, 44800, 45056, 45312, 45568, 45824,
  46080, 46336, 46592, 46848, 47104, 47360, 47616, 47872, 48128, 48384, 48640,
  48896, 49152, 49408, 49664, 49920, 50176, 50432, 50688, 50944, 51200, 51456,
  51712, 51968, 52224, 52480, 52736, 52992, 53248, 53504, 53760, 54016, 54272,
  54528, 54784, 55040, 55296, 55552, 55808, 56064, 56320, 56576, 56832, 57088,
  57344, 57600, 57856, 58112, 58368, 58624, 58880, 59136, 59392, 59648, 59904,
  60160, 60416, 60672, 60928, 61184, 61440, 61696, 61952, 62208, 62464, 62720,
  62976, 63232, 63488, 63744, 64000, 64256, 64512, 64768, 65024, 65280, 65536,
  66048, 66560, 67072, 67584, 68096, 68608, 69120, 69632, 70144, 70656, 71168,
  71680, 72192, 72704, 73216, 73728, 74240, 74752, 75264, 75776, 76288, 76800,
  77312, 77824, 78336, 78848, 79360, 79872, 80384, 80896, 81408, 81920, 82432,
  82944, 83456, 83968, 84480, 84992, 85504, 86016, 86528, 87040, 87552, 88064,
  88576, 89088, 89600, 90112, 90624, 91136, 91648, 92160, 92672, 93184, 93696,
  94208, 94720, 95232, 95744, 96256, 96768, 97280, 97792, 98304, 98816, 99328,
  99840, 100352, 100864, 101376, 101888, 102400, 102912, 103424, 103936,
  104448, 104960, 105472, 105984, 106496, 107008, 107520, 108032, 108544,
  109056, 109568, 110080, 110592, 111104, 111616, 112128, 112640, 113152,
  113664, 114176, 114688, 115200, 115712, 116224, 116736, 117248, 117760,
  118272, 118784, 119296, 119808, 120320, 120832, 121344, 121856, 122368,
  122880, 123392, 123904, 124416, 124928, 125440, 125952, 126464, 126976,
  127488, 128000, 128512, 129024, 129536, 130048, 130560, 131072, 131584,
  132096, 132608, 133120, 133632, 134144, 134656, 135168, 135680, 136192,
  136704, 137216, 137728, 138240, 138752, 139264, 139776, 140288, 140800,
  141312, 141824, 142336, 142848, 143360, 143872, 144384, 144896, 145408,
  145920, 146432, 146944, 147456, 147968, 148480, 148992, 149504, 150016,
  150528, 151040, 151552, 152064, 152576, 153088, 153600, 154112, 154624,
  155136, 155648, 156160, 156672, 157184, 157696, 158208, 158720, 159232,
  159744, 160256, 160768, 161280, 161792, 162304, 162816, 163328, 163840,
  164352, 164864, 165376, 165888, 166400, 166912, 167424, 167936, 168448,
  168960, 169472, 169984, 170496, 171008, 171520, 172032, 172544, 173056,
  173568, 174080, 174592, 175104, 175616, 176128, 176640, 177152, 177664,
  178176, 178688, 179200, 179712, 180224, 180736, 181248, 181760, 182272,
  182784, 183296, 183808, 184320, 184832, 185344, 185856, 186368, 186880,
  187392, 187904, 188416, 188928, 189440, 189952, 190464, 190976, 191488,
  192000, 192512, 193024, 193536, 194048, 194560, 195072, 195584, 196096,
  196608, 197120, 197632, 198144, 198656, 199168, 199680, 200192, 200704,
  201216, 201728, 202240, 202752, 203264, 203776, 204288, 204800, 205312,
  205824, 206336, 206848, 207360, 207872, 208384, 208896, 209408, 209920,
  210432, 210944, 211456, 211968, 212480, 212992, 213504, 214016, 214528,
  215040, 215552, 216064, 216576, 217088, 217600, 218112, 218624, 219136,
  219648, 220160, 220672, 221184, 221696, 222208, 222720, 223232, 223744,
  224256, 224768, 225280, 225792, 226304, 226816, 227328, 227840, 228352,
  228864, 229376, 229888, 230400, 230912, 231424, 231936, 232448, 232960,
  233472, 233984, 234496, 235008, 235520, 236032, 236544, 237056, 237568,
  238080, 238592, 239104, 239616, 240128, 240640, 241152, 241664, 242176,
  242688, 243200, 243712, 244224, 244736, 245248, 245760, 246272, 246784,
  247296, 247808, 248320, 248832, 249344, 249856, 250368, 250880, 251392,
  251904, 252416, 252928, 253440, 253952, 254464, 254976, 255488, 256000,
  256512, 257024, 257536, 258048, 258560, 259072, 259584, 260096, 260608,
  261120, 261632, 262144, 263168, 264192, 265216, 266240, 267264, 268288,
  269312, 270336, 271360, 272384, 273408, 274432, 275456, 276480, 277504,
  278528, 279552, 280576, 281600, 282624, 283648, 284672, 285696, 286720,
  287744, 288768, 289792, 290816, 291840, 292864, 293888, 294912, 295936,
  296960, 297984, 299008, 300032, 301056, 302080, 303104, 304128, 305152,
  306176, 307200, 308224, 309248, 310272, 311296, 312320, 313344, 314368,
  315392, 316416, 317440, 318464, 319488, 320512, 321536, 322560, 323584,
  324608, 325632, 326656, 327680, 328704, 329728, 330752, 331776, 332800,
  333824, 334848, 335872, 336896, 337920, 338944, 339968, 340992, 342016,
  343040, 344064, 345088, 346112, 347136, 348160, 349184, 350208, 351232,
  352256, 353280, 354304, 355328, 356352, 357376, 358400, 359424, 360448,
  361472, 362496, 363520, 364544, 365568, 366592, 367616, 368640, 369664,
  370688, 371712, 372736, 373760, 374784, 375808, 376832, 377856, 378880,
  379904, 380928, 381952, 382976, 384000, 385024, 386048, 387072, 388096,
  389120, 390144, 391168, 392192, 393216, 394240, 395264, 396288, 397312,
  398336, 399360, 400384, 401408, 402432, 403456, 404480, 405504, 406528,
  407552, 408576, 409600, 410624, 411648, 412672, 413696, 414720, 415744,
  416768, 417792, 418816, 419840, 420864, 421888, 422912, 423936, 424960,
  425984, 427008, 428032, 429056, 430080, 431104, 432128, 433152, 434176,
  435200, 436224, 437248, 438272, 439296, 440320, 441344, 442368, 443392,
  444416, 445440, 446464, 447488, 448512, 449536, 450560, 451584, 452608,
  453632, 454656, 455680, 456704, 457728, 458752, 459776, 460800, 461824,
  462848, 463872, 464896, 465920, 466944, 467968, 468992, 470016, 471040,
  472064, 473088, 474112, 475136, 476160, 477184, 478208, 479232, 480256,
  481280, 482304, 483328, 484352, 485376, 486400, 487424, 488448, 489472,
  490496, 491520, 492544, 493568, 494592, 495616, 496640, 497664, 498688,
  499712, 500736, 501760, 502784, 503808, 504832, 505856, 506880, 507904,
  508928, 509952, 510976, 512000, 513024, 514048, 515072, 516096, 517120,
  518144, 519168, 520192, 521216, 522240, 523264, 524288, 525312, 526336,
  527360, 528384, 529408, 530432, 531456, 532480, 533504, 534528, 535552,
  536576, 537600, 538624, 539648, 540672, 541696, 542720, 543744, 544768,
  545792, 546816, 547840, 548864, 549888, 550912, 551936, 552960, 553984,
  555008, 556032, 557056, 558080, 559104, 560128, 561152, 562176, 563200,
  564224, 565248, 566272, 567296, 568320, 569344, 570368, 571392, 572416,
  573440, 574464, 575488, 576512, 577536, 578560, 579584, 580608, 581632,
  582656, 583680, 584704, 585728, 586752, 587776, 588800, 589824, 590848,
  591872, 592896, 593920, 594944, 595968, 596992, 598016, 599040, 600064,
  601088, 602112, 603136, 604160, 605184, 606208, 607232, 608256, 609280,
  610304, 611328, 612352, 613376, 614400, 615424, 616448, 617472, 618496,
  619520, 620544, 621568, 622592, 623616, 624640, 625664, 626688, 627712,
  628736, 629760, 630784, 631808, 632832, 633856, 634880, 635904, 636928,
  637952, 638976, 640000, 641024, 642048, 643072, 644096, 645120, 646144,
  647168, 648192, 649216, 650240, 651264, 652288, 653312, 654336, 655360,
  656384, 657408, 658432, 659456, 660480, 661504, 662528, 663552, 664576,
  665600, 666624, 667648, 668672, 669696, 670720, 671744, 672768, 673792,
  674816, 675840, 676864, 677888, 678912, 679936, 680960, 681984, 683008,
  684032, 685056, 686080, 687104, 688128, 689152, 690176, 691200, 692224,
  693248, 694272, 695296, 696320, 697344, 698368, 699392, 700416, 701440,
  702464, 703488, 704512, 705536, 706560, 707584, 708608, 709632, 710656,
  711680, 712704, 713728, 714752, 715776, 716800, 717824, 718848, 719872,
  720896, 721920, 722944, 723968, 724992, 726016, 727040, 728064, 729088,
  730112, 731136, 732160, 733184, 734208, 735232, 736256, 737280, 738304,
  739328, 740352, 741376, 742400, 743424, 744448, 745472, 746496, 747520,
  748544, 749568, 750592, 751616, 752640, 753664, 754688, 755712, 756736,
  757760, 758784, 759808, 760832, 761856, 762880, 763904, 764928, 765952,
  766976, 768000, 769024, 770048, 771072, 772096, 773120, 774144, 775168,
  776192, 777216, 778240, 779264, 780288, 781312, 782336, 783360, 784384,
  785408, 786432, 787456, 788480, 789504, 790528, 791552, 792576, 793600,
  794624, 795648, 796672, 797696, 798720, 799744, 800768, 801792, 802816,
  803840, 804864, 805888, 806912, 807936, 808960, 809984, 811008, 812032,
  813056, 814080, 815104, 816128, 817152, 818176, 819200, 820224, 821248,
  822272, 823296, 824320, 825344, 826368, 827392, 828416, 829440, 830464,
  831488, 832512, 833536, 834560, 835584, 836608, 837632, 838656, 839680,
  840704, 841728, 842752, 843776, 844800, 845824, 846848, 847872, 848896,
  849920, 850944, 851968, 852992, 854016, 855040, 856064, 857088, 858112,
  859136, 860160, 861184, 862208, 863232, 864256, 865280, 866304, 867328,
  868352, 869376, 870400, 871424, 872448, 873472, 874496, 875520, 876544,
  877568, 878592, 879616, 880640, 881664, 882688, 883712, 884736, 885760,
  886784, 887808, 888832, 889856, 890880, 891904, 892928, 893952, 894976,
  896000, 897024, 898048, 899072, 900096, 901120, 902144, 903168, 904192,
  905216, 906240, 907264, 908288, 909312, 910336, 911360, 912384, 913408,
  914432, 915456, 916480, 917504, 918528, 919552, 920576, 921600, 922624,
  923648, 924672, 925696, 926720, 927744, 928768, 929792, 930816, 931840,
  932864, 933888, 934912, 935936, 936960, 937984, 939008, 940032, 941056,
  942080, 943104, 944128, 945152, 946176, 947200, 948224, 949248, 950272,
  951296, 952320, 953344, 954368, 955392, 956416, 957440, 958464, 959488,
  960512, 961536, 962560, 963584, 964608, 965632, 966656, 967680, 968704,
  969728, 970752, 971776, 972800, 973824, 974848, 975872, 976896, 977920,
  978944, 979968, 980992, 982016, 983040, 984064, 985088, 986112, 987136,
  988160, 989184, 990208, 991232, 992256, 993280, 994304, 995328, 996352,
  997376, 998400, 999424, 1000448, 1001472, 1002496, 1003520, 1004544, 1005568,
  1006592, 1007616, 1008640, 1009664, 1010688, 1011712, 1012736, 1013760,
  1014784, 1015808, 1016832, 1017856, 1018880, 1019904, 1020928, 1021952,
  1022976, 1024000, 1025024, 1026048, 1027072, 1028096, 1029120, 1030144,
  1031168, 1032192, 1033216, 1034240, 1035264, 1036288, 1037312, 1038336,
  1039360, 1040384, 1041408, 1042432, 1043456, 1044480, 1045504, 1046528,
  1047552, 1048576
};

#define KLISSMALLBIN(B) (B < KLSMALLBIN)

#define KLSQR(V) ((V)*(V))

#define KLSIZE2BIN(S)                                                       \
  (                                                                         \
    assert(0 != (S)),                                                       \
    ((S)<=64)                                                               \
      ? ((S)-1)/8                                                           \
      : (KLLOG2((S)-1)<20)                                                  \
        ? ((S)>=KLSQR(log2size[KLLOG2((S)-1)-1])+1)                         \
          ? log2off[KLLOG2((S)-1)] +                                        \
            ((S)-(KLSQR(log2size[KLLOG2((S)-1)-1])+1)) /                    \
            log2size[KLLOG2((S)-1)]                                         \
          : log2off[KLLOG2((S)-1)-1] +                                      \
            ((S)-(KLSQR(log2size[KLLOG2((S)-1)-2])+1)) /                    \
            log2size[KLLOG2((S)-1)-1]                                       \
        : log2off[KLLOG2((S)-1)]                                            \
  )

#define KLBIN2SIZE(B) (assert((B)<KLSMALLBIN), bin2size[(B)])


/****************************************************************************/
/* System memory allocation related macros */
/****************************************************************************/
#define KL_SYS_ALLOC_FAIL                -1
#define KL_CALL_SYS_ALLOC_ALIGNED(P,A,S) posix_memalign(&(P),A,S)
#define KL_CALL_SYS_FREE(P,S)            free(P)


/****************************************************************************/
/****************************************************************************/
/* Free chunk data structure API */
/****************************************************************************/
/****************************************************************************/

/****************************************************************************/
/* Free chunk data structure node */
/****************************************************************************/
typedef struct kl_bin_node
{
  struct kl_bin_node * p; /* previous node */
  struct kl_bin_node * n; /* next node */
} kl_bin_node_t;


/****************************************************************************/
/* Free chunk data structure */
/****************************************************************************/
typedef struct kl_bin
{
  int init;
  struct kl_bin_node * bin[KLNUMBIN];
} kl_bin_t;


/****************************************************************************/
/* Initialize free chunk data structure */
/****************************************************************************/
static int
kl_bin_init(kl_bin_t * const bin)
{
  int i;

  for (i=0; i<KLNUMBIN; ++i)
    bin->bin[i] = NULL;

  bin->init = 1;

  return 0;
}


/****************************************************************************/
/* Add a node to a free chunk data structure */
/****************************************************************************/
static int
kl_bin_ad(kl_bin_t * const bin, void * const chunk)
{
  size_t bidx = KLSIZE2BIN(KL_C2S(chunk));
  kl_bin_node_t * p, * n, * node = KL_C2D(chunk);

  KL_PRINT("==klinfo== insert chunk\n");
  KL_PRINT("==klinfo==   block address: 0x%.16zx\n", (uptr)KL_C2B(chunk));
  KL_PRINT("==klinfo==   chunk address: 0x%.16zx\n", (uptr)chunk);
  KL_PRINT("==klinfo==   chunk size:    0x%.16zx (%zu)\n", KL_C2S(chunk),
    KL_C2S(chunk));
  KL_PRINT("==klinfo==   bin index:     %zu\n", bidx);

  /* Treat fixed size bins and large bins differently */
  if (KLISSMALLBIN(bidx)) {
    /* Sanity check: bin[bidx] is empty or the previous pointer of its head
     * node is NULL. */
    assert(NULL == bin->bin[bidx] || NULL == bin->bin[bidx]->p);
    /* Sanity check: node is not the head of bin[bidx]. */
    assert(node != bin->bin[bidx]);
    /* Sanity check: node has no dangling pointers. */
    assert(NULL == node->p && NULL == node->n);
    /* Sanity check: node is not in use. */
    assert(KL_C2S(chunk) == KL_C2F(chunk));

    /* Shift bin index in case a chunk is made up of coalesced chunks which
     * collectively have a size which causes the chunk to fall in a particular
     * bin, but has less bytes than required by the bin.
     * TODO: it should be possible to do this with an if statement, since it
     * should shift down by one bin at most. */
    while (KL_C2S(chunk) < KLBIN2SIZE(bidx) && bidx > 0)
      bidx--;

    /* Sanity check: chunk must be at least the size of the fixed bin. */
    assert(KL_C2S(chunk) >= KLBIN2SIZE(bidx));

    KL_PRINT("==klinfo==   new bin index: %zu\n", bidx);

    /* Prepend n to front of bin[bidx] linked-list. */
    node->p = NULL;
    node->n = bin->bin[bidx];
    if (NULL != bin->bin[bidx]) {
      assert(NULL == bin->bin[bidx]->p);
      bin->bin[bidx]->p = node;
    }
    bin->bin[bidx] = node;
  }
  else {
    /* This will keep large buckets sorted. */
    n = bin->bin[bidx];
    p = NULL;

    while (NULL != n && KL_P2S(n) < KL_C2S(chunk)) {
      p = n;
      n = n->n;
    }

    if (NULL != n) {
      /* insert internally */
      node->p = n->p;
      node->n = n;
      if (NULL == n->p)
        bin->bin[bidx] = node;
      else
        n->p->n = node;
      n->p = node;
    }
    else if (NULL != p) {
      /* insert at the end */
      p->n = node;
      node->p = p;
      node->n = NULL;
    }
    else {
      /* insert at the beginning */
      bin->bin[bidx] = node;
      node->p = NULL;
      node->n = NULL;
    }
  }

  return 0;
}


/****************************************************************************/
/* Remove a node from a free chunk data structure */
/****************************************************************************/
static int
kl_bin_rm(kl_bin_t * const bin, void * const chunk)
{
  size_t bidx = KLSIZE2BIN(KL_C2S(chunk));
  kl_bin_node_t * node = KL_C2D(chunk);

  KL_PRINT("==klinfo== remove chunk\n");
  KL_PRINT("==klinfo==   block address: 0x%.16zx\n", (uptr)KL_C2B(chunk));
  KL_PRINT("==klinfo==   chunk address: 0x%.16zx\n", (uptr)chunk);
  KL_PRINT("==klinfo==   chunk size:    0x%.16zx (%zu)\n", KL_C2S(chunk),
    KL_C2S(chunk));
  KL_PRINT("==klinfo==   bin index:     %zu\n", bidx);

  /* Shift bin index in case a chunk is made up of coalesced chunks which
   * collectively have a size which causes the chunk to fall in a particular
   * bin, but has less bytes than required by the bin.
   * TODO: it should be possible to do this with an if statement, since it
   * should shift down by one bin at most. */
  while (KL_C2S(chunk) < KLBIN2SIZE(bidx) && bidx > 0)
    bidx--;

  KL_PRINT("==klinfo==   new bin index: %zu\n", bidx);

  /* Sanity check: node has correct pointer structure. */
  assert(NULL != node->p || NULL != node->n || bin->bin[bidx] == node);

  /* Fixed and variable sized bins are treated the same, since removing a node
   * from a variable sized bin will not cause it to become unsorted. */
  if (NULL == node->p)
    bin->bin[bidx] = node->n;
  else
    node->p->n = node->n;
  if (NULL != node->n)
    node->n->p = node->p;
  node->n = NULL;
  node->p = NULL;

  /* Sanity check: bin[bidx] is empty or the previous pointer of its head node
   * is NULL. */
  assert(NULL == bin->bin[bidx] || NULL == bin->bin[bidx]->p);

  return 0;
}


/****************************************************************************/
/* Find the bin with the smallest size >= size parameter */
/****************************************************************************/
static void *
kl_bin_find(kl_bin_t * const bin, size_t const size)
{
  size_t bidx = KLSIZE2BIN(size);
  kl_bin_node_t * n;

  if (KLISSMALLBIN(bidx)) {
    /* Find first bin with a node. */
    n = bin->bin[bidx];
    while (NULL == n && KLISSMALLBIN(bidx))
      n = bin->bin[++bidx];

    /* Remove head of bin[bidx]. */
    if (NULL != n) {
      assert(NULL != bin->bin[bidx]);
      assert(NULL == n->p);
      bin->bin[bidx] = n->n;
      if (NULL != n->n) {
        n->n->p = NULL;
        n->n = NULL;
      }

      KL_PRINT("==klinfo== found chunk\n");
      KL_PRINT("==klinfo==   block address:   0x%.16zx\n", (uptr)KL_P2B(n));
      KL_PRINT("==klinfo==   chunk address:   0x%.16zx\n", (uptr)KL_P2C(n));
      KL_PRINT("==klinfo==   chunk size:      0x%.16zx (%zu)\n", KL_P2S(n),
        KL_P2S(n));
      KL_PRINT("==klinfo==   request size:    0x%.16zx (%zu)\n", size, size);
      KL_PRINT("==klinfo==   request log:     %zu\n", KLLOG2(size-1));
      KL_PRINT("==klinfo==   request log2off: %zu\n", log2off[KLLOG2(size-1)]);
      KL_PRINT("==klinfo==   request logsize: %zu\n", log2size[KLLOG2(size-1)]);
      KL_PRINT("==klinfo==   bin index:       %zu\n", bidx);
      KL_PRINT("==klinfo==   bin size:        %zu\n", KLBIN2SIZE(bidx));

      /* Sanity check: chunk must be large enough to hold request. */
      assert(size <= KL_P2S(n));
    }

    assert(NULL == bin->bin[bidx] || NULL == bin->bin[bidx]->p);
  }
  else {
    /* Find first bin with a node. */
    n = bin->bin[bidx];
    while (NULL == n && bidx < KLNUMBIN)
      n = bin->bin[++bidx];

    assert(NULL == n);

    /* Find first node in bin[bidx] with size >= size parameter. */
    while (NULL != n && KL_P2S(n) < size)
      n = n->n;

    /* Remove n from bin[bidx]. */
    if (NULL != n) {
      if (NULL == n->p) {
        bin->bin[bidx] = n->n;
        if (NULL != n->n)
          n->n->p = NULL;
      }
      else {
        n->p->n = n->n;
        if (NULL != n->n)
          n->n->p = n->p;
      }
      n->p = NULL;
      n->n = NULL;
    }
  }

  return (NULL == n) ? NULL : KL_P2C(n);
}


/****************************************************************************/
/****************************************************************************/
/* KL API */
/****************************************************************************/
/****************************************************************************/

/****************************************************************************/
/* Free chunk data structure */
/****************************************************************************/
static kl_bin_t bin={.init=0};


/****************************************************************************/
/* Initialize static variables and data structures */
/****************************************************************************/
#define KL_INIT_CHECK                                                       \
do {                                                                        \
  if (0 == bin.init)                                                        \
    kl_bin_init(&bin);                                                      \
} while (0)


/****************************************************************************/
/* Allocate size bytes of memory */
/****************************************************************************/
KL_EXPORT void *
klmalloc(size_t const size)
{
  int ret;
  size_t bsize;
  void * block, * chunk, * ptr;

  /* TODO: need to allocate chunks according to the fixed size bins, not just
   * according to the KL_MEMORY_ALLOCATION_ALIGNMENT. */

  /*
      Basic algorithm:
        If a memory chunk >= aligned size of request is available:
          1. Use the chunk
        Otherwise, when no such chunk is available:
          1. If small request (< 65536 - per-block overhead), get a new block
             from system and use it
          2. Otherwise, get the required memory plus per-block overhead from
             system and use it
        If the chunk has excess bytes greater than inactive-chunk overhead:
          1. Split the chunk into two chunks
  */

  KL_INIT_CHECK;

  KL_PRINT("==klinfo== allocation request\n");
  KL_PRINT("==klinfo==   request size: 0x%.16zx (%zu)\n",
    KL_CHUNK_SIZE_ALIGNED(size), KL_CHUNK_SIZE_ALIGNED(size));

  /* Check for available memory chunk. */
  if (NULL == (chunk=kl_bin_find(&bin, KL_CHUNK_SIZE_ALIGNED(size)))) {
    /* Determine appropriate allocation size. */
    if (KL_CHUNK_SIZE_ALIGNED(size) <= KL_BLOCK_SIZE_ALIGNED-KL_BLOCK_HEADER_SIZE_ALIGNED)
      bsize = KL_BLOCK_SIZE_ALIGNED;
    else
      bsize = KL_BLOCK_HEADER_SIZE_ALIGNED+KL_CHUNK_SIZE_ALIGNED(size);

    /* Get system memory */
    ret = KL_CALL_SYS_ALLOC_ALIGNED(block, KL_BLOCK_SIZE_ALIGNED, bsize);
    if (KL_SYS_ALLOC_FAIL == ret)
      return NULL;

    /* Zero memory */
    memset(block, 0, bsize);

    /* Set block size */
    KL_B2S(block) = bsize;

    /* Set the chunk to be returned */
    chunk = KL_B2C(block);

    /* Temporarily set chunk size */
    KL_C2S(chunk) = bsize-KL_BLOCK_HEADER_SIZE_ALIGNED;

    /* Sanity check: macros are working. */
    assert(block == KL_C2B(chunk));
    /* Sanity check: block is properly aligned. */
    assert(0 == ((uptr)block&(KL_BLOCK_SIZE_ALIGNED-1)));
    /* Sanity check: chunk is properly aligned. */
    assert(0 == ((uptr)chunk&(KL_MEMORY_ALLOCATION_ALIGNMENT-1)));

    KL_PRINT("==klinfo== new block\n");
    KL_PRINT("==klinfo==   block address: 0x%.16zx\n", (uptr)block);
    KL_PRINT("==klinfo==   block size:    0x%.16zx (%zu)\n", bsize, bsize);
    KL_PRINT("==klinfo== new chunk\n");
    KL_PRINT("==klinfo==   chunk address: 0x%.16zx\n", (uptr)chunk);
    KL_PRINT("==klinfo==   chunk size:    0x%.16zx (%zu)\n",
      KL_CHUNK_SIZE_ALIGNED(size), KL_CHUNK_SIZE_ALIGNED(size));
  }
  else {
    KL_PRINT("==klinfo== old chunk\n");
    KL_PRINT("==klinfo==   chunk address: 0x%.16zx\n", (uptr)chunk);
    KL_PRINT("==klinfo==   chunk size:    0x%.16zx (%zu)\n",
      KL_C2S(chunk), KL_C2S(chunk));
  }

  /* Split chunk if applicable. */
  if (KL_C2S(chunk) > KL_CHUNK_MIN_ALIGNED &&
      KL_CHUNK_SIZE_ALIGNED(size) <= KL_C2S(chunk)-KL_CHUNK_MIN_ALIGNED)
  {
    /* Set chunk[1] size. */
    KL_C2S(KL_CS2T(chunk, size)) = KL_C2S(chunk)-KL_CHUNK_SIZE_ALIGNED(size);

    /* Set chunk[1] as not in use. */
    KL_C2F(KL_CS2T(chunk, size)) = KL_C2S(chunk)-KL_CHUNK_SIZE_ALIGNED(size);

    KL_PRINT("==klinfo== split chunk\n");
    KL_PRINT("==klinfo==   block address: 0x%.16zx\n", (uptr)KL_C2B(chunk));
    KL_PRINT("==klinfo==   chunk address: 0x%.16zx\n",
      (uptr)KL_CS2T(chunk, size));
    KL_PRINT("==klinfo==   chunk size:    0x%.16zx (%zu)\n",
      KL_C2S(KL_CS2T(chunk, size)), KL_C2S(KL_CS2T(chunk, size)));

    /* Add chunk[1] to free chunk data structure.  In case NDEBUG is not
     * defined, the pointers should be reset.  This is necessary whenever
     * memory chunks have been coallesced, since the memory locations that the
     * pointers occupy may have been modified if one of the coalesced chunks
     * was used as an allocation. */
#ifndef NDEBUG
    memset(KL_C2P(KL_CS2T(chunk, size)), 0, sizeof(kl_bin_node_t));
#endif
    if (0 != kl_bin_ad(&bin, KL_CS2T(chunk, size)))
      return NULL;

    /* Update chunk[0] size. */
    KL_C2S(chunk) = KL_CHUNK_SIZE_ALIGNED(size);

    /* Sanity check: chunk[0] can be reached from chunk[1] */
    assert((KL_C2F(chunk) = KL_CHUNK_SIZE_ALIGNED(size),
      chunk == KL_T2C(KL_C2T(chunk))));
  }

  /* Set chunk[0] as in use */
  KL_C2F(chunk) = 0;

  /* Increment count for containing block. */
  KL_B2N(KL_C2B(chunk))++;

  /* Get pointer to return. */
  ptr = KL_C2P(chunk);

  /* Sanity check: macros are working. */
  assert(chunk == KL_P2C(ptr));
  /* Sanity check: returned pointer is properly aligned. */
  assert(0 == ((uptr)ptr&(KL_MEMORY_ALLOCATION_ALIGNMENT-1)));
  /* Sanity check: chunk points to a valid piece of memory. */
  assert((uptr)chunk+KL_C2S(chunk) <= (uptr)KL_C2B(chunk)+KL_B2S(KL_C2B(chunk)));
  /* Sanity check: pointer points to a valid piece of memory. */
  assert((uptr)ptr+size <= (uptr)KL_P2B(ptr)+KL_B2S(KL_P2B(ptr)));

  return ptr;
}


/****************************************************************************/
/* Allocate num*size bytes of zeroed memory */
/****************************************************************************/
KL_EXPORT void *
klcalloc(size_t const num, size_t const size)
{
  KL_INIT_CHECK;

  void * ptr=klmalloc(num*size);
  if (NULL != ptr)
    memset(ptr, 0, num*size);
  return ptr;
}


/****************************************************************************/
/* Release size bytes of memory */
/****************************************************************************/
KL_EXPORT void
klfree(void * const ptr)
{
  void * chunk = KL_P2C(ptr);
  void * block = KL_C2B(chunk);

  /* TODO: one issue with current implementation is the following.  When a
   * large block gets split into smaller blocks, the ability to use the KL_C2B
   * macro goes away, since the large block could get split up such that a
   * chunk beyond the KL_BLOCK_SIZE_ALIGNED limit is allocated, thus when
   * KL_C2B is called it will return a memory location of something which is
   * not a block.  To address this, large blocks are not allowed to be split.
   * */
  /* TODO: another issue with current implementation is the following.  When a
   * block count gets decremented to zero, its associated memory is freed.
   * However, nowhere does the system account for any chunks that remain in
   * the free chunk data structure.  This is especially problematic because
   * those chunks still in the free chunk data structure cannot be removed in
   * place, due to the singly-linked nature of the data structure. */

  KL_INIT_CHECK;

  /* Sanity check to make sure the containing block is somewhat valid. */
  assert(0 != KL_B2N(block));

  /* TODO: Implicitly, the following rule prevents large allocations from
   * going into the free chunk data structure.  Thus, it also prevents the
   * limitation described above.  However, in many cases, it would be nice to
   * keep large allocations around for quicker allocation time. */

  KL_PRINT("==klinfo== free chunk\n");
  KL_PRINT("==klinfo==   block address: 0x%.16zx\n", (uptr)block);
  KL_PRINT("==klinfo==   chunk address: 0x%.16zx\n", (uptr)chunk);
  KL_PRINT("==klinfo==   chunk size:    0x%.16zx (%zu)\n", KL_C2S(chunk),
    KL_C2S(chunk));

  /* Decrement count for containing block and release entire block if it is
   * empty. */
  if (0 == --KL_B2N(block)) {
    assert(KL_ISLAST(chunk) || !KL_INUSE(KL_C2T(chunk)));

    /* Remove previous chunk from free chunk data structure. */
    if (!KL_ISFIRST(chunk) && 0 != kl_bin_rm(&bin, KL_T2C(chunk)))
      return;
    /* Remove following chunk from free chunk data structure. */
    if (!KL_ISLAST(chunk) && 0 != kl_bin_rm(&bin, KL_C2T(chunk)))
      return;

    KL_PRINT("==klinfo== free block\n");
    KL_PRINT("==klinfo==   block address: 0x%.16zx\n", (uptr)block);
    KL_PRINT("==klinfo==   block size:    0x%.16zx (%zu)\n", KL_B2S(block),
      KL_B2S(block));

    KL_CALL_SYS_FREE(block, KL_B2S(block));
  }
  /* Otherwise, add the chunk back into free chunk data structure. */
  else {
    /* Coalesce with previous chunk. */
    if (!KL_ISFIRST(chunk) && !KL_INUSE(KL_T2C(chunk))) {
      /* Remove previous chunk from free chunk data structure. */
      if (0 != kl_bin_rm(&bin, KL_T2C(chunk)))
        return;

      /* Set chunk to point to previous chunk. */
      chunk = KL_T2C(chunk);

      /* Update chunk size. */
      KL_C2S(chunk) = KL_C2S(chunk) + KL_C2S(KL_C2T(chunk));
    }

    /* Coalesce with following chunk. */
    if (!KL_ISLAST(chunk) && !KL_INUSE(KL_C2T(chunk))) {
      /* Remove following chunk from free chunk data structure. */
      if (0 != kl_bin_rm(&bin, KL_C2T(chunk)))
        return;

      /* Update chunk size. */
      KL_C2S(chunk) = KL_C2S(chunk) + KL_C2S(KL_C2T(chunk));
    }

    /* Set chunk as not in use. */
    KL_C2F(chunk) = KL_C2S(chunk);

    /* Add chunk to free chunk data structure. */
    if (0 != kl_bin_ad(&bin, chunk))
      return;
  }
}
