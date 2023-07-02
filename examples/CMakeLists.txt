add_executable(pause pause.cpp)

target_include_directories(pause PRIVATE ${CMAKE_SOURCE_DIR})

add_executable(threadpool threadpool.cpp)

target_include_directories(threadpool PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(threadpool pthread)