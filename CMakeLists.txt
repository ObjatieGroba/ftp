cmake_minimum_required(VERSION 3.13)
project(ftp)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

FILE(GLOB FTP_SOURCES main.cpp)
add_executable(main ${FTP_SOURCES})