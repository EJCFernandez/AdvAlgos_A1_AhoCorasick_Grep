cmake_minimum_required(VERSION 3.16)

# Whether the user asked for a build type has to be sampled BEFORE project():
# project() itself seeds the cache with CMAKE_BUILD_TYPE=Debug on some
# CMake/compiler combinations (observed with CMake 4.4.3 and MSVC), and once it
# has, "did the user leave it empty?" can no longer be answered. Testing
# emptiness after project() silently gave a Debug build, which would have made
# the benchmark numbers meaningless.
if(CMAKE_BUILD_TYPE)
  set(ahogrep_build_type_given TRUE)
else()
  set(ahogrep_build_type_given FALSE)
endif()

project(ahogrep LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # no GNU extensions, so MSVC and g++ see the same language

# Release unless the user chose otherwise. Skipped for multi-config generators
# (Visual Studio, Ninja Multi-Config), which ignore CMAKE_BUILD_TYPE and pick at
# build time via --config.
if(NOT ahogrep_build_type_given AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CONFIGURATION_TYPES)
  message(STATUS "ahogrep: multi-config generator, build with --config Release")
else()
  message(STATUS "ahogrep: build type is ${CMAKE_BUILD_TYPE}")
endif()

# Warning flags in one place, attached to every target below.
add_library(ahogrep_flags INTERFACE)
if(MSVC)
  target_compile_options(ahogrep_flags INTERFACE /W4 /permissive- /utf-8)
  # The code deliberately uses portable fopen/fread rather than MSVC-only
  # fopen_s, so C4996 on fopen is noise here, not a finding.
  target_compile_definitions(ahogrep_flags INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(ahogrep_flags INTERFACE -Wall -Wextra)
endif()

# The automaton: no I/O, no CLI, so the tests can link it on its own.
add_library(aho_corasick STATIC src/aho_corasick.cpp)
target_include_directories(aho_corasick PUBLIC include)
target_link_libraries(aho_corasick PUBLIC ahogrep_flags)

add_executable(ahogrep
  src/main.cpp
  src/cli.cpp
  src/io.cpp
)
target_link_libraries(ahogrep PRIVATE aho_corasick ahogrep_flags)

enable_testing()

add_executable(test_random tests/test_random.cpp)
target_link_libraries(test_random PRIVATE aho_corasick ahogrep_flags)
add_test(NAME test_random COMMAND test_random)
# A failure-link walk written without the root guard -- `while (no child) state =
# fail_[state];` -- spins forever at the root instead of returning a wrong
# answer. A timeout is what turns that hang into a reported failure.
set_tests_properties(test_random PROPERTIES TIMEOUT 120)

add_executable(test_edge tests/test_edge.cpp)
target_link_libraries(test_edge PRIVATE aho_corasick ahogrep_flags)
add_test(NAME test_edge COMMAND test_edge)
# Still a stub: exit code 77 makes ctest report Skipped rather than Passed, so a
# green run cannot claim coverage that does not exist yet.
set_tests_properties(test_edge PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
