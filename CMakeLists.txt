cmake_minimum_required(VERSION 3.8)
project(ftp_server)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

include_directories(include)

set(SOURCE_FILES src/main.c include/reactor.h include/ftp_server.h src/ftp_server.c)
add_executable(ftp-server ${SOURCE_FILES})