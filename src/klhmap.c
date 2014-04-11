#include "klhmap.h"

#define KL_WITH_QUAD

#define KL_NPRIMES       256
/*#define KL_HASH(X, MASK) ((uptr)(X) & (MASK))*/
#define KL_HASH(X, MASK) ((uptr)(X) % (MASK))

#if 0
static void
kl_hmap_grow(
  kl_hmap_t * const hm
)
{
  void * tmp;

  if (sp->ssz >= sp->scap) {
    tmp = KL_MMAP(sp->scap*2*sizeof(kl_splay_t *));
    memcpy(tmp, sp->smem, sp->scap*sizeof(kl_splay_t *));
    KL_MUNMAP(sp->smem, sp->scap*sizeof(kl_splay_t *));
    sp->scap *= 2;
    sp->smem  = (kl_splay_t **) tmp;
  }

  tmp = KL_MMAP(sp->cap*sizeof(kl_splay_t));
  sp->mem = sp->smem[sp->ssz++] = (kl_splay_t *) tmp;
  sp->sz  = 0;
}
#endif

void
kl_hmap_init(
  kl_hmap_t * const hm
)
{
  /*size_t i;*/
  hm->cap = 4 * KL_PAGESIZE / KL_HMAP_SIZE;

  /*for (i=0; i<KL_NPRIMES; ++i) {
    if (hm->cap >= prm_tbl[i]) break;
  }
  hm->cap = prm_tbl[i];*/
  /*hm->cap = 4093;*/
  hm->cap = 999983;
  hm->msk = hm->cap; /* this should be removed for masking hash function */
  hm->mem = KL_MMAP(hm->cap * KL_HMAP_SIZE);
}

void
kl_hmap_free(
  kl_hmap_t * const hm
)
{
  KL_MUNMAP(hm->mem, hm->cap * KL_HMAP_SIZE);
  hm->mem = NULL;
  hm->cap = 0;
  hm->msk = 0;
}

int
kl_hmap_insert(
  kl_hmap_t * const hm,
  uptr const blk,
  u32 const pid,
  u32 const bid
)
{
  size_t i, k = KL_HASH(blk, hm->msk);
  void   * p;

  for (i=0; i<hm->cap; ++i) {
    p = (char *)hm->mem + (k * KL_HMAP_SIZE);
    if (!(*((uptr *)p)) || (*((uptr *)p)) == blk) {
      *(uptr *)p = blk;
      p = (uptr *)p + 1;
      *(u32 *)p = pid;
      p = (u32 *)p + 1;
      *(u32 *)p = bid;
      /*printf("%lu\n", i);*/
      return 1;
    }
#ifdef KL_WITH_QUAD
    k = (k + i * i) % hm->cap;
#else
    k = (k + 1) % hm->cap;
#endif
  }

  return 0;
}

int
kl_hmap_find(
  kl_hmap_t * const hm,
  uptr const blk,
  u32 * const pid,
  u32 * const bid
)
{
  size_t i, k = KL_HASH(blk, hm->msk);
  void   * p;

  for (i=0; i<hm->cap; ++i) {
    p = (char *)hm->mem + (k * KL_HMAP_SIZE);
    if (!(*((uptr *)p))) {
      return 0;
    }else if ((*((uptr *)p)) == blk) {
      p = (uptr *)p + 1;
      *pid = *(u32 *)p;
      p = (u32 *)p + 1;
      *bid = *(u32 *)p;
      return 1;
    }
#ifdef KL_WITH_QUAD
    k = (k + i * i) % hm->cap;
#else
    k = (k + 1) % hm->cap;
#endif
  }

  return 0;
}

void
kl_hmap_delete(
  kl_hmap_t * const hm,
  uptr const blk
)
{
  size_t i, k = KL_HASH(blk, hm->msk);
  void   * p;

  for (i=0; i<hm->cap; ++i) {
    p = (char *)hm->mem + (k * KL_HMAP_SIZE);
    if (!(*((uptr *)p))) {
      return;
    }else if ((*((uptr *)p)) == blk) {
      *(uptr *)p = 0;
      return;
    }
#ifdef KL_WITH_QUAD
    k = (k + i * i) % hm->cap;
#else
    k = (k + 1) % hm->cap;
#endif
  }
}

#ifdef KL_WITH_MAIN

size_t KL_VMEM = 0, KL_MXVMEM = 0;

int
main()
{
  u32       pid, bid;
  kl_hmap_t hm;

  kl_hmap_init(&hm);

  kl_hmap_insert(&hm, 1, 1, 2);
  kl_hmap_insert(&hm, 3, 3, 4);
  kl_hmap_insert(&hm, 8, 8, 9);
  kl_hmap_insert(&hm, 11, 11, 12);
  kl_hmap_insert(&hm, 14, 14, 15);

  kl_hmap_find(&hm, 1, &pid, &bid);
  printf("1 %u %u\n", pid, bid);
  kl_hmap_find(&hm, 3, &pid, &bid);
  printf("3 %u %u\n", pid, bid);
  kl_hmap_find(&hm, 8, &pid, &bid);
  printf("8 %u %u\n", pid, bid);
  kl_hmap_find(&hm, 11, &pid, &bid);
  printf("11 %u %u\n", pid, bid);
  kl_hmap_find(&hm, 14, &pid, &bid);
  printf("14 %u %u\n", pid, bid);

  kl_hmap_delete(&hm, 1);

  if (kl_hmap_find(&hm, 1, &pid, &bid)) {
    printf("delete failed\n");
  }

  kl_hmap_free(&hm);

  return EXIT_SUCCESS;
}
#endif
