# Post-build check: the firmware image must not contain a heap allocator.
execute_process(COMMAND arm-none-eabi-nm ${ELF} OUTPUT_VARIABLE SYMS)
foreach(sym malloc _malloc_r calloc realloc free _free_r pvPortMalloc _sbrk)
    if(SYMS MATCHES "[ \t]+[TtWw][ \t]+${sym}\n")
        message(FATAL_ERROR "Dynamic allocation symbol '${sym}' found in ${ELF}")
    endif()
endforeach()
message(STATUS "check_no_heap: OK, no allocator linked")
