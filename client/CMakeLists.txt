add_executable(chat_client WIN32
  main.cpp
  chat_client.cpp
  chat_window.cpp
)

target_link_libraries(chat_client PRIVATE ws2_32)

set_target_properties(chat_client PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)