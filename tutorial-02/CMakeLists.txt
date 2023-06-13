cmake_minimum_required(VERSION 3.25)
project(tutorial_02)

set(CMAKE_CXX_STANDARD 14)

include_directories(../libs/ffmpeg/include)
include_directories(../libs/SDL/include)

link_directories(../libs/ffmpeg/lib)
link_directories(../libs/SDL/lib/x64)

add_executable(tutorial_02 error-code.h main.cpp)

target_link_libraries(${PROJECT_NAME} SDL2main SDL2 libavcodec libavformat libavutil libswscale libswresample)
