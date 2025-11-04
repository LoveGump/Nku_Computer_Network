cmake_minimum_required(VERSION 3.20)
project(ChatApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 使用Unicode on Windows
if(WIN32)
  add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN)
endif()

# 强制MSVC将源文件视为UTF-8，以避免C4819警告
if(MSVC)
  add_compile_options(/utf-8)
endif()

include_directories(${CMAKE_SOURCE_DIR}/src)

add_subdirectory(server)
add_subdirectory(client)
