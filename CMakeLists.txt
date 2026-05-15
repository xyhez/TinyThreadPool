cmake_minimum_required(VERSION 3.10)
project(ThreadPool VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(ThreadPool STATIC ThreadPool.cpp)
target_link_libraries(ThreadPool PUBLIC Threads::Threads)
target_include_directories(ThreadPool PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ThreadPool>
)

add_executable(normal_test testcase/normal_test.cpp)
target_link_libraries(normal_test PRIVATE ThreadPool)

add_executable(stress_test testcase/stress_test.cpp)
target_link_libraries(stress_test PRIVATE ThreadPool)

add_executable(edge_test testcase/edge_test.cpp)
target_link_libraries(edge_test PRIVATE ThreadPool)

# ====== 安装规则 ======
include(GNUInstallDirs)

install(TARGETS ThreadPool
    EXPORT ThreadPoolTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ThreadPool.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ThreadPool
)

install(EXPORT ThreadPoolTargets
    NAMESPACE ThreadPool::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadPool
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ThreadPoolConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadPool
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadPool
)
