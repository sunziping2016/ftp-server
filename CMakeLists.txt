cmake_minimum_required(VERSION 3.8)
project(ftp_server)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

include_directories(include)

set(SOURCE_FILES src/main.c include/reactor.h
        include/global.h src/global.c
        include/ftp_server.h src/ftp_server.c
        include/ftp_client.h src/ftp_client.c
        include/ftp_cli.h src/ftp_cli.c)
add_executable(ftp-server ${SOURCE_FILES})
target_link_libraries(ftp-server readline)
