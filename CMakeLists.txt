cmake_minimum_required(VERSION 3.15)

project(channels)

find_package(Threads REQUIRED)

if(BUILD_DOCS)
    add_subdirectory(docs)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_TESTS)
    add_subdirectory(googletest)
    add_subdirectory(tests)
endif()

add_library(channels INTERFACE)
target_include_directories(channels INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(channels INTERFACE Threads::Threads)