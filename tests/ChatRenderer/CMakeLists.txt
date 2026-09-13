cmake_minimum_required(VERSION 3.28)
project(PulseChatTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
add_executable(PulseChatTests main.cpp)
target_link_libraries(PulseChatTests PRIVATE Qt6::Widgets Qt6::Network)

# Keep this standalone test executable runnable from Explorer as well as from
# the prepared build shell.  That prevents a misleading missing-Qt dialog when
# a developer launches the test artifact directly.
if(WIN32)
  add_custom_command(TARGET PulseChatTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:Qt6::Core>"
      "$<TARGET_FILE:Qt6::Gui>"
      "$<TARGET_FILE:Qt6::Widgets>"
      "$<TARGET_FILE:Qt6::Network>"
      "$<TARGET_FILE_DIR:PulseChatTests>"
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:PulseChatTests>/platforms")
  foreach(platform_plugin QWindowsIntegrationPlugin QMinimalIntegrationPlugin)
    if(TARGET Qt6::${platform_plugin})
      add_custom_command(TARGET PulseChatTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:Qt6::${platform_plugin}>"
          "$<TARGET_FILE_DIR:PulseChatTests>/platforms")
    endif()
  endforeach()
endif()
