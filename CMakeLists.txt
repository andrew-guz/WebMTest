cmake_minimum_required(VERSION 3.16)

project(WebMTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_library(AVFORMAT_LIB avformat)
find_library(AVCODEC_LIB avcodec)
find_library(AVUTIL_LIB avutil)
find_library(SWSCALE_LIB swscale)

add_subdirectory(vendor)

add_executable(${PROJECT_NAME} src/main.cpp) 

target_link_libraries(${PROJECT_NAME} ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWSCALE_LIB} Crow::Crow)
