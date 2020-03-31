cmake_minimum_required(VERSION 3.13)
project(ftp)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

FILE(GLOB FTP_SERVER_SOURCES server.cpp)
add_executable(server ${FTP_SERVER_SOURCES})

FILE(GLOB FTP_CLIENT_SOURCES client.cpp)
add_executable(client ${FTP_CLIENT_SOURCES})