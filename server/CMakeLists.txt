add_executable(chat_server
  main.cpp
  chat_server.cpp
)

target_link_libraries(chat_server PRIVATE ws2_32)

set_target_properties(chat_server PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)