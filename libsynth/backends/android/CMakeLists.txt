# Point an Android app at this file from its module build.gradle:
#
#   android {
#       defaultConfig { minSdk 26 }          // AAudio
#       externalNativeBuild {
#           cmake { path "../../libsynth/backends/android/CMakeLists.txt" }
#       }
#       sourceSets.main.java.srcDirs += "../../libsynth/backends/android/java"
#   }
#
# Devices below API 26 have no AAudio; those need Oboe, which falls back to
# OpenSL ES and can drop in behind this same file.

cmake_minimum_required(VERSION 3.18)
project(synthjni C)

set(SYNTH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SYNTH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SYNTH_BUILD_PC_PLAYBACK OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_BINARY_DIR}/libsynth)

# Tracks are engines, and the bridge mixes them into the one stream: a groovebox
# wants a patch and a voice pool per part, and the core's answer to that is
# several synth_t rather than a channel argument. Both counts are compile-time,
# because the library never allocates: SYNTH_TRACKS engines of SYNTH_MAX_VOICES
# voices each, which the app reads back as SynthEngine.trackCount().
#
# What it costs, measured with tools/bench-block on one 96-frame block: four
# tracks with two voices sounding each is 127,000 instructions where the same
# eight voices in a single engine are 105,000, and four tracks with nothing
# playing at all is 29,700. That floor is the price of one shared clock — every
# track is rendered on every callback, or its frame numbers fall behind the
# others' — and it is proportional to SYNTH_MAX_VOICES, since a block scans
# every slot whether or not it sounds. At SYNTH_MAX_VOICES=4 the same floor is
# 20,500. A phone has cycles to spare either way; this is the knob to reach for
# if a low-end device starts glitching.
set(SYNTH_TRACKS 4 CACHE STRING "Independent engines the bridge mixes into one stream")

add_library(synthjni SHARED synth_jni.c)
target_compile_definitions(synthjni PRIVATE SYNTH_TRACKS=${SYNTH_TRACKS})
target_link_libraries(synthjni PRIVATE synth aaudio log)

if(NOT MSVC)
    target_compile_options(synthjni PRIVATE -Wall -Wextra)
endif()
