cmake_minimum_required(VERSION 3.28)
project(PulseOverlayAlertTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core)

get_filename_component(REPO_ROOT "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(OVERLAY_SOURCE "${REPO_ROOT}/engine/obs-studio/plugins/pulse-weaver-core")

add_executable(PulseOverlayAlertTests
  main.cpp
  "${OVERLAY_SOURCE}/pulse-overlay-alerts.cpp"
  "${OVERLAY_SOURCE}/pulse-overlay-alerts.hpp"
  "${OVERLAY_SOURCE}/pulse-overlay-renderer.cpp"
)
target_include_directories(PulseOverlayAlertTests PRIVATE "${OVERLAY_SOURCE}")
target_link_libraries(PulseOverlayAlertTests PRIVATE Qt6::Core)

enable_testing()
add_test(NAME OverlayAlerts COMMAND PulseOverlayAlertTests)
