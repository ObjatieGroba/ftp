cmake_minimum_required(VERSION 3.13)
project(ftp)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

FILE(GLOB FTP_SOURCES main.cpp "include/*.hpp")
add_executable(server ${FTP_SOURCES})

target_link_libraries(server pam)

FILE(GLOB TEST_SOURCES test.cpp "include/*.hpp")
add_executable(tests ${TEST_SOURCES})

target_link_libraries(tests pthread)
