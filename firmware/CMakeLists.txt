# Target firmware for STM32F446RE (Nucleo-F446RE / Renode).

FetchContent_Declare(freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    GIT_TAG V11.1.0
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(freertos_kernel)
if(NOT freertos_kernel_POPULATED)
    # Sources only: the kernel's own CMake always builds a heap_x.c, but this
    # firmware has no heap (configSUPPORT_DYNAMIC_ALLOCATION = 0).
    FetchContent_Populate(freertos_kernel)
endif()
set(RTOS ${freertos_kernel_SOURCE_DIR})

add_library(freertos STATIC
    ${RTOS}/tasks.c
    ${RTOS}/queue.c
    ${RTOS}/list.c
    ${RTOS}/portable/GCC/ARM_CM4F/port.c)
target_include_directories(freertos PUBLIC
    ${RTOS}/include
    ${RTOS}/portable/GCC/ARM_CM4F
    ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_compile_options(freertos PRIVATE -w)  # third-party: warnings not ours to fix

set(BSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bsp/stm32f446)

add_executable(firmware
    app/main.c
    app/sensor_task.c
    app/packer_task.c
    app/tx_task.c
    app/monitor_task.c
    ${BSP_DIR}/bsp.c
    ${BSP_DIR}/bsp_i2c.c
    ${BSP_DIR}/startup_stm32f446.c)
set_target_properties(firmware PROPERTIES SUFFIX ".elf")
target_include_directories(firmware PRIVATE app ${BSP_DIR})
target_link_libraries(firmware PRIVATE telemetry_core freertos)
target_compile_options(firmware PRIVATE ${PROJECT_WARNINGS} -include ${NO_HEAP_HEADER})
# The kernel headers use a few constructs our strict flags dislike.
target_compile_options(firmware PRIVATE -Wno-cast-align -Wno-sign-conversion)
target_link_options(firmware PRIVATE
    -T${BSP_DIR}/STM32F446RETx.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/firmware.map
    -Wl,--print-memory-usage)

add_custom_command(TARGET firmware POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:firmware> ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:firmware>
    # Fail the build if anything pulled in an allocator.
    COMMAND ${CMAKE_COMMAND} -DELF=$<TARGET_FILE:firmware> -P ${CMAKE_SOURCE_DIR}/cmake/check_no_heap.cmake
    VERBATIM)
