cmake_minimum_required(VERSION 3.10)
project(ucifs C)

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS "-Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-g -DDEBUG=1")

include_directories( . )

add_executable(ucifs ucifs.c logStuff.c logStuff.h uci2libelektra.c uci2libelektra.h)

target_link_libraries(ucifs fuse3 uci)

install(TARGETS ucifs
        RUNTIME DESTINATION /usr/bin)
