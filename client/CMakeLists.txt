# 添加可执行文件目标
add_executable(chat_client WIN32
  main.cpp
  chat_client.cpp
  chat_window.cpp
)

# 添加源文件目录
target_link_libraries(chat_client PRIVATE ws2_32)

# 设置输出目录
set_target_properties(chat_client PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)