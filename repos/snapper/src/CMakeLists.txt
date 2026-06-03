cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(Snapper)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(VERBOSE "Display verbose output." OFF)
if (VERBOSE)
  add_definitions(-DVERBOSE)
endif()

if(NOT DEFINED ENV{TESTS})
    set(ENV{TESTS} 1000)
endif()

if(NOT DEFINED ENV{PAYLOAD_NUM})
    set(ENV{PAYLOAD_NUM} 100)
endif()

if(NOT DEFINED ENV{PAYLOAD_SIZE})
    set(ENV{PAYLOAD_SIZE} 1)
endif()

add_definitions(-DXXH_NO_STREAM)
add_definitions(-DTESTS=$ENV{TESTS})
add_definitions(-DPAYLOAD_NUM=$ENV{PAYLOAD_NUM})
add_definitions(-DPAYLOAD_SIZE=$ENV{PAYLOAD_SIZE})

# Main Executables
add_executable(snapper snapper/main.cc)
add_executable(snappertests test/snapper/main.cc)
add_executable(snapperbench bench/snapper/main.cc)

# Snapper Library (libsnap)
add_library(snap
  lib/snapper.cc
  lib/archive.cc
  lib/backlink.cc
  lib/utils.cc
  lib/xxhash32.cc
)

# Linking
target_include_directories(snap PUBLIC ../include)

target_link_libraries(snapper PUBLIC snap)

target_link_libraries(snappertests PUBLIC snap)

target_link_libraries(snapperbench PUBLIC snap)

# Target-Specific Compiler Options
target_compile_options(snapper PRIVATE -O3)
target_compile_options(snapperbench PRIVATE -fno-omit-frame-pointer -g)
