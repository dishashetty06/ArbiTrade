cmake_minimum_required(VERSION 3.20)
project(arbitrade VERSION 1.0.0 LANGUAGES CXX)

# --- C++ Standard ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if the user didn't specify a build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# --- Global include path so any target can do #include "common/types.hpp" ---
include_directories(${CMAKE_SOURCE_DIR}/src)

# --- MSVC specific compiler flags ---
if(MSVC)
    add_compile_options(/W4 /EHsc /MP)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# --- Subprojects ---
add_subdirectory(src/replayer)
add_subdirectory(src/agent)

message(STATUS "ArbiTrade configured: CXX=${CMAKE_CXX_STANDARD}, BuildType=${CMAKE_BUILD_TYPE}")