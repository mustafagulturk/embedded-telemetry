cmake_minimum_required(VERSION 3.20)

project(embedded_telemetry LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Warnings shared by firmware and host builds of project code. Third-party
# code (FreeRTOS kernel, Unity) is compiled without -Werror on purpose.
set(PROJECT_WARNINGS
    -Wall -Wextra -Werror -pedantic
    -Wshadow -Wconversion -Wsign-conversion -Wcast-qual -Wcast-align
    -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wswitch-default
    -Wdouble-promotion -Wformat=2 -Wnull-dereference)

set(NO_HEAP_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/firmware/core/include/no_heap.h)

# Portable code: built for both the target and the host.
add_library(telemetry_core STATIC
    firmware/core/src/crc16.c
    firmware/core/src/packet.c
    firmware/core/src/altitude.c
    firmware/core/src/alt_filter.c
    firmware/core/src/health.c
    firmware/core/src/supervisor.c
    firmware/drivers/src/mpu6050.c
    firmware/drivers/src/bmp280.c)
target_include_directories(telemetry_core PUBLIC
    firmware/core/include
    firmware/drivers/include)
target_compile_options(telemetry_core PRIVATE ${PROJECT_WARNINGS} -include ${NO_HEAP_HEADER})
target_link_libraries(telemetry_core PUBLIC m)

if(CMAKE_CROSSCOMPILING)
    add_subdirectory(firmware)
else()
    option(TELEMETRY_SANITIZERS "Build host tests with ASan/UBSan" ON)
    option(TELEMETRY_COVERAGE "Build host tests with gcov instrumentation" OFF)
    if(TELEMETRY_SANITIZERS)
        target_compile_options(telemetry_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(telemetry_core PUBLIC -fsanitize=address,undefined)
    endif()
    if(TELEMETRY_COVERAGE)
        target_compile_options(telemetry_core PUBLIC --coverage)
        target_link_options(telemetry_core PUBLIC --coverage)
    endif()
    enable_testing()
    add_subdirectory(tests)
endif()
