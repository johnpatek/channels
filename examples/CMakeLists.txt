add_executable(pause pause.cpp)
add_executable(threadpool threadpool.cpp)

target_link_libraries(pause channels)
target_link_libraries(threadpool channels)

if (MSVC)
    target_compile_options(pause PRIVATE /W4 /WX)
    target_compile_options(threadpool PRIVATE /W4 /WX)
else()
    target_compile_options(pause PRIVATE -Wall -Wextra -Werror)
    target_compile_options(threadpool PRIVATE -Wall -Wextra -Werror)
endif()