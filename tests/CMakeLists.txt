add_executable(unit unit.cpp)

target_include_directories(unit PRIVATE ${CMAKE_SOURCE_DIR}/googletest/googletest/include ${CMAKE_SOURCE_DIR})

target_link_libraries(unit gtest gtest_main)

add_executable(integration integration.cpp)

target_include_directories(integration PRIVATE ${CMAKE_SOURCE_DIR}/googletest/googletest/include ${CMAKE_SOURCE_DIR})

target_link_libraries(integration gtest gtest_main)