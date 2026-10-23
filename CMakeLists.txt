cmake_minimum_required(VERSION 3.16)
project(hft_instr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

set(HFT_INSTR_SOURCES
    src/registry.cpp
    src/recorder.cpp
    src/decoder.cpp
)

add_library(hft_instr STATIC ${HFT_INSTR_SOURCES})
target_include_directories(hft_instr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hft_instr PUBLIC Threads::Threads)
target_compile_options(hft_instr PRIVATE -Wall -Wextra -Wpedantic)

add_executable(hft_instr_decode src/decode_main.cpp)
target_link_libraries(hft_instr_decode PRIVATE hft_instr)
target_compile_options(hft_instr_decode PRIVATE -Wall -Wextra -Wpedantic)

add_executable(hft_instr_tests tests/test_instr.cpp)
target_link_libraries(hft_instr_tests PRIVATE hft_instr)
target_compile_options(hft_instr_tests PRIVATE -Wall -Wextra -Wpedantic)

# Separate library so the steady_clock fallback is what the test binary runs,
# including recorder startup, not only the inlined marker.
add_library(hft_instr_fallback STATIC ${HFT_INSTR_SOURCES})
target_include_directories(hft_instr_fallback PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hft_instr_fallback PUBLIC Threads::Threads)
target_compile_definitions(hft_instr_fallback PUBLIC HFT_INSTR_FORCE_STEADY_CLOCK)
target_compile_options(hft_instr_fallback PRIVATE -Wall -Wextra -Wpedantic)

add_executable(hft_instr_tests_fallback tests/test_fallback.cpp)
target_link_libraries(hft_instr_tests_fallback PRIVATE hft_instr_fallback)
target_compile_options(hft_instr_tests_fallback PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME instr_tests COMMAND hft_instr_tests)
add_test(NAME instr_tests_fallback COMMAND hft_instr_tests_fallback)
add_test(
    NAME disabled_codegen
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_disabled_codegen.py
            ${CMAKE_CXX_COMPILER}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/disabled_codegen.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_BINARY_DIR}/disabled_codegen.s
)
