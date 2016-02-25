cmake_minimum_required (VERSION 2.8)
project (SBMA)

option (USE_THREAD "Enable thread-safety components of SBMA library" OFF)

add_library (
  sbma
  api/hooks.c api/calloc.c api/destroy.c api/free.c api/init.c api/mallinfo.c
  api/malloc.c api/mallopt.c api/mcheck.c api/mclear.c api/mevict.c
  api/mexist.c api/mtouch.c api/parse_optstr.c api/realloc.c api/remap.c
  api/sigoff.c api/sigon.c api/timeinfo.c api/vinit.c
  ipc/atomic_dec.c ipc/atomic_inc.c ipc/destroy.c ipc/init.c ipc/is_eligible.c
  ipc/madmit.c ipc/mdirty.c ipc/mevict.c ipc/sigoff.c ipc/sigon.c
  klmalloc/klmalloc.c
  lock/free.c lock/get.c lock/init.c lock/let.c
  mmu/destroy.c mmu/init.c mmu/insert_ate.c mmu/invalidate_ate.c
  mmu/lookup_ate.c
  vmm/destroy.c vmm/init.c vmm/swap_i.c vmm/swap_o.c vmm/swap_x.c
)

if (USE_THREAD)
  set_target_properties(sbma PROPERTIES COMPILE_DEFINITIONS USE_THREAD)
endif (USE_THREAD)

# LINSTALL_PATH and HINSTALL_PATH are only relevant if this is being built as
# part of a Karypis project.
install (TARGETS sbma DESTINATION lib/${LINSTALL_PATH})
install (FILES include/sbma.h DESTINATION include/${HINSTALL_PATH})

#add_executable (test/micro1 test/micro1.c)
#target_link_libraries(test/micro1 sbma m dl rt pthread)
