cmake_minimum_required(VERSION 3.28)
project(PulseBroadcastIsolation LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(PulseBroadcastIsolation main.cpp)
target_link_libraries(PulseBroadcastIsolation PRIVATE Qt6::Core)
enable_testing()
add_test(NAME BroadcastIsolation COMMAND PulseBroadcastIsolation
    "${CMAKE_CURRENT_SOURCE_DIR}/../../engine/obs-studio/frontend/forms/OBSYoutubeActions.ui")
