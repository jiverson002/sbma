cmake_minimum_required (VERSION 2.8)
project (SBMalloc)

option (USE_THREAD "Enable thread-safety components of SBMA library" OFF)

add_library (
  sbma
  hooks.c ipc.c klmalloc.c lock.c malloc.c mcntrl.c mextra.c mmu.c
  mstate.c vmm.c
)

if (USE_THREAD)
  set_target_properties(sbma PROPERTIES COMPILE_DEFINITIONS USE_THREAD)
endif (USE_THREAD)

# LINSTALL_PATH and HINSTALL_PATH are only relevant if this is being built as
# part of a Karypis project.
install (TARGETS sbma DESTINATION lib/${LINSTALL_PATH})
install (FILES sbma.h DESTINATION include/${HINSTALL_PATH})

#add_executable (test/micro1 test/micro1.c)
#target_link_libraries(test/micro1 sbma m dl rt pthread)
