cmake_minimum_required(VERSION 3.16)

# Work around a CMake 4.1.x regression on some Windows setups where CXX source
# file extensions are not initialized early enough for try_compile(), causing:
# "Unknown extension .cpp/.cxx for file ... try_compile() works only for enabled languages".
if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL "4.1")
  if(NOT DEFINED CMAKE_CXX_SOURCE_FILE_EXTENSIONS OR CMAKE_CXX_SOURCE_FILE_EXTENSIONS STREQUAL "")
    set(CMAKE_CXX_SOURCE_FILE_EXTENSIONS
        C;M;c++;cc;cpp;cxx;m;mm;mpp;CPP;ixx;cppm;ccm;cxxm;c++m
        CACHE STRING "C++ source file extensions" FORCE)
  endif()
endif()

project(ReliableUDPTransport LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(sender
    src/sender_main.cpp
    src/sender.cpp
    src/rtp.cpp
    src/congestion_control.cpp
    src/send_window.cpp
    src/transfer_stats.cpp
    src/utils/logger.cpp
)

add_executable(receiver
    src/receiver_main.cpp
    src/receiver.cpp
    src/rtp.cpp
    src/receive_buffer.cpp
    src/transfer_stats.cpp
    src/utils/logger.cpp
)

if(WIN32)
  target_link_libraries(sender PRIVATE ws2_32)
  target_link_libraries(receiver PRIVATE ws2_32)
endif()
