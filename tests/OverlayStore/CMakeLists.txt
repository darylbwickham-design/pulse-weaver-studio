cmake_minimum_required(VERSION 3.28)
project(PulseOverlayStoreTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core)

get_filename_component(REPO_ROOT "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(OVERLAY_SOURCE "${REPO_ROOT}/engine/obs-studio/plugins/pulse-weaver-core")

add_executable(PulseOverlayStoreTests
  main.cpp
  "${OVERLAY_SOURCE}/pulse-overlay-store.cpp"
  "${OVERLAY_SOURCE}/pulse-overlay-store.hpp"
)
target_include_directories(PulseOverlayStoreTests PRIVATE "${OVERLAY_SOURCE}")
target_link_libraries(PulseOverlayStoreTests PRIVATE Qt6::Core)

enable_testing()
add_test(NAME OverlayStore COMMAND PulseOverlayStoreTests)
