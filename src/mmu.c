#define MMU_MMAX (1lu<<40)

#define MMAP_PROT  PROT_READ|PROT_WRITE
#define MMAP_FLAGS MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_LOCK

struct {
  size_t  page_size;      /*!< mmu page size */
  size_t  min_alloc_size; /*!< mmu minimum allocation size */
  char *  page_table;     /*!< mmu page table */
  void ** alloc_table;    /*!< mmu allocation table */
#ifdef USE_PTHREAD
  pthread_mutex_t lock;   /*!< mutex guarding struct */
#endif
} mmu;

extern int
mmu_init(struct mmu * const __mmu, size_t const __page_size,
         size_t const __min_alloc_size)
{
  int ret;
  size_t pt_len, at_len;

  /* initialize variables */
  ret                = -1;
  __mmu->page_table  = MAP_FAILED;
  __mmu->alloc_table = MAP_FAILED;

  /* compute table lentghts */
  pt_len = MMU_MMAX/__page_size;
  at_len = MMU_MMAX/__min_alloc_size;;

  /* populate mmu variables */
  __mmu->page_size   = __page_size;
  __mmu->page_table  = (char*)mmap(NULL, pt_len, MMAP_PROT, MMAP_FLAGS, -1, 0);
  __mmu->alloc_table = (void**)mmap(NULL, at_len, MMAP_PROT, MMAP_FLAGS, -1, 0);
#ifdef USE_PTHREAD
  ret = pthread_mutex_init(&(__mmu->lock), NULL);
#endif

  if (MAP_FAILED == __mmu->page_table)
    goto cleanup;
  if (MAP_FAILED == __mmu->alloc_table)
    goto cleanup;
#ifdef USE_PTHRAD
  if (-1 == ret)
    goto cleanup;
#endif

  return 0;

  cleanup:
  if (MAP_FAILED != __mmu->page_table)
    (void)munmap(__mmu->page_table, pt_len);
  if (MAP_FAILED != __mmu->alloc_table)
    (void)munmap(__mmu->alloc_table, at_len);
#ifdef USE_PTHREAD
  if (-1 != ret)
    (void)pthread_mutex_destroy(&(__mmu->lock));
#endif

  return -1;
}

extern int
mmu_destroy(struct mmu * const __mmu)
{
  int ret;
  size_t pt_len, at_len;

  /* compute table lentghts */
  pt_len = MMU_MMAX/__page_size;
  at_len = MMU_MMAX/__min_alloc_size;;

  (void)munmap(__mmu->page_table, pt_len);
  (void)munmap(__mmu->alloc_table, at_len);
#ifdef USE_PTHREAD
  (void)pthread_mutex_destroy(&(__mmu->lock));
#endif

  return 0;
}
