cmake_minimum_required(VERSION 3.14)
project(ftp)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

FILE(GLOB FTP_SOURCES main.cpp "include/*.hpp")
add_executable(ftp ${FTP_SOURCES})

FILE(GLOB TEST_SOURCES test.cpp "include/*.hpp")
add_executable(test ${TEST_SOURCES})

target_link_libraries(ftp pam)
