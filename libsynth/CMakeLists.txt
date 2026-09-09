cmake_minimum_required(VERSION 3.13)
project(libsynth C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(SYNTH_BUILD_TESTS "Build the host test suite" ON)
set(SYNTH_MAX_VOICES "8" CACHE STRING "Maximum polyphony, fixed at compile time")

add_library(synth STATIC
    src/dsp.c
    src/synth.c
)

target_include_directories(synth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(synth PUBLIC SYNTH_MAX_VOICES=${SYNTH_MAX_VOICES})

if(NOT MSVC)
    target_compile_options(synth PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(SYNTH_BUILD_TESTS)
    enable_testing()
    add_executable(test_synth tests/test_synth.c)
    target_link_libraries(test_synth PRIVATE synth)
    if(UNIX)
        target_link_libraries(test_synth PRIVATE m)
    endif()
    add_test(NAME synth_core COMMAND test_synth)
endif()
