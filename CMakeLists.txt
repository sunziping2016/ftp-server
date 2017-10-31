cmake_minimum_required(VERSION 3.0)
project(ftp_server)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

include_directories(include)

set(SOURCE_FILES src/main.c include/reactor.h
        include/global.h src/global.c
        include/ftp_server.h src/ftp_server.c
        include/ftp_client.h src/ftp_client.c
        include/ftp_cli.h src/ftp_cli.c
        include/signal_handler.h src/signal_handler.c
        include/ftp_users.h src/ftp_users.c
        include/ftp_timer.h src/ftp_timer.c)

add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/bcrypt/bcrypt.a
        COMMAND make bcrypt.a
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/bcrypt)
add_custom_target(bcrypt.a DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/bcrypt/bcrypt.a)

add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/hash_table/hash_table.o
        COMMAND ./autogen.sh
        COMMAND make hash_table.o
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/hash_table)
add_custom_target(hash_table.o DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/hash_table/hash_table.o)

add_executable(ftp-server ${SOURCE_FILES})
add_dependencies(ftp-server bcrypt.a hash_table.o)
target_link_libraries(ftp-server readline
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/bcrypt/bcrypt.a
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/hash_table/hash_table.o)

