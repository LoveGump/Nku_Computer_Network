add_executable(chat_client WIN32
  main.cpp
)

if(WIN32)
  target_link_libraries(chat_client PRIVATE ws2_32)
endif()

set_target_properties(chat_client PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)