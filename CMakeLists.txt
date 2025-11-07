cmake_minimum_required(VERSION 3.20)
project(ChatApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # 禁用编译器扩展

# 全局定义与编译选项
add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
# MSVC UTF-8 源码
add_compile_options(/utf-8)

include_directories(${CMAKE_SOURCE_DIR}/src)

# 添加子目录
add_subdirectory(server)
add_subdirectory(client)
