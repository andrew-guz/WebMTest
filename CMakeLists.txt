cmake_minimum_required(VERSION 3.16)

project(WebMTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

find_package(FFmpeg REQUIRED COMPONENTS
    avformat
    avcodec
    avutil
    swscale
    swresample)

add_subdirectory(vendor)

add_executable(${PROJECT_NAME}
    src/AVContext.cpp
    src/main.cpp) 

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(${PROJECT_NAME} PRIVATE
    FFMPEG::avformat
    FFMPEG::avcodec
    FFMPEG::avutil
    FFMPEG::swscale
    FFMPEG::swresample
    Crow::Crow)
