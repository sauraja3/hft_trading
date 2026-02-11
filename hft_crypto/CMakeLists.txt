cmake_minimum_required(VERSION 3.14)
project(hft_crypto LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

option(BUILD_PYTHON_BINDINGS "Build pybind11 Python bindings" ON)

# Core library (no Python dep)
add_library(hft_core STATIC
    src/signals.cpp
    src/position.cpp
    src/order_logic.cpp
    src/market_types.cpp
)
target_include_directories(hft_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Python bindings (optional)
if(BUILD_PYTHON_BINDINGS)
    find_package(pybind11 CONFIG QUIET)
    if(NOT pybind11_FOUND)
        find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
        include(FetchContent)
        FetchContent_Declare(pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.11.1
        )
        FetchContent_MakeAvailable(pybind11)
    endif()
    pybind11_add_module(hft_py bindings/pybind.cpp)
    target_link_libraries(hft_py PRIVATE hft_core)
endif()

# Standalone engine executable (for production)
add_executable(hft_engine main/engine_main.cpp)
target_link_libraries(hft_engine PRIVATE hft_core)
