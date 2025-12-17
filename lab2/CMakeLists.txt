  cmake_minimum_required(VERSION 3.16)
  project(ReliableUDPTransport LANGUAGES CXX)

  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)

  include_directories(include)  # 或 target_include_directories

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