cmake_minimum_required(VERSION 3.16)
project(ReliableUDPTransport LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加include目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(rtp_common STATIC
    src/rtp.cpp
)
target_include_directories(rtp_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(sender
    src/sender_main.cpp
    src/sender.cpp
)
target_link_libraries(sender PRIVATE rtp_common)

add_executable(receiver
    src/receiver_main.cpp
    src/receiver.cpp
)
target_link_libraries(receiver PRIVATE rtp_common)

# if(WIN32)
#     target_link_libraries(sender PRIVATE ws2_32)
#     target_link_libraries(receiver PRIVATE ws2_32)
# endif()

