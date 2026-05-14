cmake_minimum_required(VERSION 3.10)
project(ThreadPool)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(ThreadPool STATIC ThreadPool.cpp)
target_link_libraries(ThreadPool PUBLIC Threads::Threads)

add_executable(normal_test testcase/normal_test.cpp)
target_link_libraries(normal_test PRIVATE ThreadPool)

add_executable(stress_test testcase/stress_test.cpp)
target_link_libraries(stress_test PRIVATE ThreadPool)
