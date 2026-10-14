cmake_minimum_required(VERSION 3.15)
project(kvstore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SOURCES "src/*.cpp")
add_library(kvstore STATIC ${SOURCES})
target_include_directories(kvstore PUBLIC include)

# 给库也加上 sanitizer（编译 + 链接）
target_compile_options(kvstore PRIVATE -Wall -Wextra -g -fsanitize=address,undefined)
target_link_options(kvstore PRIVATE -fsanitize=address,undefined)

add_executable(main src/main.cpp)
target_link_libraries(main kvstore)

# 给 main 也加上 sanitizer（编译 + 链接）
target_compile_options(main PRIVATE -Wall -Wextra -g -fsanitize=address,undefined)
target_link_options(main PRIVATE -fsanitize=address,undefined)