include(FetchContent)

# Header-only, pinned, and fetched rather than vendored so the repo stays small.
FetchContent_Declare(miniaudio
    URL https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(miniaudio)

add_executable(play_pc play_pc.c)
target_link_libraries(play_pc PRIVATE synth)
target_include_directories(play_pc PRIVATE
    ${miniaudio_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/examples
)

if(UNIX AND NOT APPLE)
    # miniaudio dlopens ALSA/PulseAudio at runtime, so no -dev package is needed.
    target_link_libraries(play_pc PRIVATE dl pthread m)
elseif(APPLE)
    target_link_libraries(play_pc PRIVATE
        "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()
