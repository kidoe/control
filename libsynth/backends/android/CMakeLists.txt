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

add_library(synthjni SHARED synth_jni.c)
target_link_libraries(synthjni PRIVATE synth aaudio log)

if(NOT MSVC)
    target_compile_options(synthjni PRIVATE -Wall -Wextra)
endif()
