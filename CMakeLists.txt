cmake_minimum_required(VERSION 3.14)
project(ftp)

set(CMAKE_CXX_STANDARD 17)

add_executable(ftp main.cpp)
target_link_libraries(ftp pam)
