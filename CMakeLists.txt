cmake_minimum_required(VERSION 3.15)
project(kvstore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 把 src 下所有 .cpp 打包成静态库（已经没有 main.cpp 了）
file(GLOB SOURCES "src/*.cpp")
add_library(kvstore STATIC ${SOURCES})
target_include_directories(kvstore PUBLIC include)

# 压测程序：开启 O2 优化，不带 Sanitizer，保证性能数据真实
add_executable(bench bench/bench.cpp)
target_link_libraries(bench kvstore)
target_compile_options(bench PRIVATE -Wall -Wextra -O2)