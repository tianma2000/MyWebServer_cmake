cmake_minimum_required(VERSION 3.18)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
project(CppCMakeWebserver LANGUAGES C CXX)

add_subdirectory(lock)
add_subdirectory(threadpool)
add_subdirectory(http)
add_subdirectory(timer)
add_subdirectory(log)
add_subdirectory(cgimysql)

add_executable(a.out main.cpp)
target_link_libraries(a.out PUBLIC lock threadpool http timer log cgimysql )

