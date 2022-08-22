cmake_minimum_required(VERSION 3.10)
project(ucifs C)

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS "-Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-g -DDEBUG=1")

include_directories( . )

find_package(Elektra REQUIRED)

if (ELEKTRA_FOUND)
    message (STATUS "Elektra ${ELEKTRA_VERSION} found")
    include_directories (${ELEKTRA_INCLUDE_DIR})

else (ELEKTRA_FOUND)
    message (FATAL_ERROR "Elektra not found")
endif (ELEKTRA_FOUND)

add_executable(ucifs ucifs.c logStuff.c logStuff.h uci2libelektra.c uci2libelektra.h)

target_link_libraries(ucifs fuse3 uci ${ELEKTRA_LIBRARIES})

install(TARGETS ucifs
        RUNTIME DESTINATION /usr/bin)
