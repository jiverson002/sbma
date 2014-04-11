#ifndef KLMALLINFO_H
#define KLMALLINFO_H

#ifdef __cplusplus
extern "C"
{
#endif

struct klmallinfo {
  unsigned long pgsz;     /* internal page size                         */
  unsigned long init;     /* init status of klmalloc mpool              */
  unsigned long splaysz;  /* number of nodes in use in splay tree       */
  unsigned long splaycap; /* capacity of splay tree                     */
  unsigned long poolact;  /* active entries in mpool                    */
  unsigned long poolsz;   /* number of blocks in use in all pools       */
  unsigned long poolcap;  /* block capacity of all pools                */
  unsigned long dpqsz;    /* number of nodes in use in discrete pq      */
  unsigned long dpqcap;   /* capacity of discrete pq                    */
  unsigned long ovrhd;    /* memory used by klmalloc data structures    */
  unsigned long vmem;     /* virtual memory use as recorded by klmalloc */
  unsigned long rmem;     /* maximum requested memory at a given time   */
  unsigned long over;     /* amount of memory allocated beyond request  */
};

void   klstats();
struct klmallinfo klmallinfo();

#ifdef __cplusplus
}
#endif

#endif
