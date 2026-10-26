FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(unity)

add_library(test_support STATIC support/fake_i2c.c)
target_include_directories(test_support PUBLIC support)
target_link_libraries(test_support PUBLIC telemetry_core unity)
target_compile_options(test_support PRIVATE -Wall -Wextra -Werror)

set(TESTS crc16 packet altitude alt_filter health supervisor mpu6050 bmp280)
foreach(t IN LISTS TESTS)
    add_executable(test_${t} test_${t}.c)
    target_link_libraries(test_${t} PRIVATE test_support)
    target_compile_options(test_${t} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${t} COMMAND test_${t})
endforeach()
