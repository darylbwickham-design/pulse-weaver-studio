cmake_minimum_required(VERSION 3.28...3.30)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

option(PULSEWEAVER_KICK_PROTOCOL_TESTS "Build Kick protocol regression tests" OFF)
if(PULSEWEAVER_KICK_PROTOCOL_TESTS)
  add_executable(pulse-weaver-kick-protocol-tests tests/PulseKickProtocolTests.cpp)
  target_link_libraries(pulse-weaver-kick-protocol-tests PRIVATE Qt::Network)
  add_test(NAME pulse-weaver-kick-protocol-tests COMMAND pulse-weaver-kick-protocol-tests)
endif()

add_library(pulse-weaver-core MODULE)
add_library(PulseWeaver::core ALIAS pulse-weaver-core)

target_sources(
  pulse-weaver-core
  PRIVATE pulse-weaver-core.cpp
          pulse-motion-engine.cpp
          pulse-motion-engine.hpp
          pulse-overlay-alerts.cpp
          pulse-overlay-alerts.hpp
          pulse-overlay-renderer.cpp
          pulse-overlay-renderer.hpp
          pulse-overlay-migration.cpp
          pulse-overlay-migration.hpp
          pulse-overlay-runtime.cpp
          pulse-overlay-runtime.hpp
          pulse-overlay-store.cpp
          pulse-overlay-store.hpp
)

target_link_libraries(
  pulse-weaver-core
  PRIVATE OBS::libobs OBS::frontend-api Qt::Widgets Qt::Network
)

if(PULSEWEAVER_MOTION_PREVIEW)
  target_compile_definitions(pulse-weaver-core PRIVATE PULSEWEAVER_MOTION_PREVIEW=1)
endif()

if(OS_WINDOWS)
  target_link_libraries(pulse-weaver-core PRIVATE winhttp crypt32)
  configure_file(cmake/windows/obs-module.rc.in pulse-weaver-core.rc)
  target_sources(pulse-weaver-core PRIVATE pulse-weaver-core.rc)
endif()

set_target_properties_obs(
  pulse-weaver-core
  PROPERTIES FOLDER plugins/pulse-weaver-core
             PREFIX ""
             AUTOMOC ON
)
