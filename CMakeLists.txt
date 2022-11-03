cmake_minimum_required(VERSION 3.22)
project(FFmpeg_and_SDL_decoder)

set(CMAKE_CXX_STANDARD 14)

set(SDL_INCLUDE C:/libs/SDL/include)
set(SDL_LIB C:/libs/SDL/lib/x64)

set(FFMPEG_INCLUDE C:/libs/ffmpeg/include)
set(FFMPEG_LIB C:/libs/ffmpeg/lib)

include_directories(${SDL_INCLUDE})
include_directories(${FFMPEG_INCLUDE})
link_directories(${SDL_LIB})
link_directories(${FFMPEG_LIB})

add_executable(FFmpeg_and_SDL_decoder sync-video-with-audio-custom.cpp)

target_link_libraries(${PROJECT_NAME}
        SDL2main
        SDL2
        libavcodec
        libavdevice
        libavfilter
        libavformat
        libavutil
        libpostproc
        libswresample
        libswscale
        )