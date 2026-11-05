cmake_minimum_required(VERSION 3.15)
project(lab2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(process_lib STATIC process_manager.h process_manager.cpp)

add_executable(test_runner main.cpp)
target_link_libraries(test_runner PRIVATE process_lib)