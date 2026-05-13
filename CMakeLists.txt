cmake_minimum_required(VERSION 3.10)
project(ThreadPool)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

set(THREAD_POOL_SOURCES
    ThreadPool.cpp
    ThreadPool.h
        testcase/available_object.h
)

add_executable(normal_test
    testcase/normal_test.cpp
    ${THREAD_POOL_SOURCES}
        testcase/available_object.h
)
target_link_libraries(normal_test PRIVATE Threads::Threads)

add_executable(stress_test
    testcase/stress_test.cpp
    ${THREAD_POOL_SOURCES}
        testcase/available_object.h
)
target_link_libraries(stress_test PRIVATE Threads::Threads)
