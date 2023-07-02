cmake_minimum_required(VERSION 3.0)

project(channels)

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