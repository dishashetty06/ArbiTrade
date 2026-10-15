cmake_minimum_required(VERSION 3.20)
project(arbitrade VERSION 4.0.0 LANGUAGES CXX)

# --- C++ Standard ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# --- Global include path: all public headers live under include/<module>/...
include_directories(${CMAKE_SOURCE_DIR}/include)

# --- MSVC specific compiler flags ---
if(MSVC)
    add_compile_options(/W4 /EHsc /MP)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# -----------------------------------------------------------------------
# Core library: every shared, transport-agnostic module (networking,
# orderbook, strategy) is compiled exactly ONCE here as a static
# library. replayer.exe, agent.exe, and lock_server.exe all link
# against it, so there is a single source of truth for this code and no
# duplicate compilation across binaries.
# -----------------------------------------------------------------------
add_library(arbitrade_core STATIC
    src/networking/tcp_server.cpp
    src/networking/tcp_client.cpp
    src/orderbook/order_book.cpp
    src/strategy/spread_detector.cpp
)

target_include_directories(arbitrade_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(arbitrade_core PUBLIC ws2_32)

# --- Subprojects ---
add_subdirectory(src/replayer)
add_subdirectory(src/agent)
add_subdirectory(src/lock_server)

message(STATUS "ArbiTrade configured: CXX=${CMAKE_CXX_STANDARD}, BuildType=${CMAKE_BUILD_TYPE}")