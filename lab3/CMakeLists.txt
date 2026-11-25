cmake_minimum_required(VERSION 3.15)
project(lab3 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(lab3 main.c platform.c)

if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(lab3 PRIVATE Threads::Threads)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(lab3 PRIVATE rt)
    endif()
endif()
