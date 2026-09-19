cmake_minimum_required(VERSION 3.28...3.30)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_library(pulse-weaver-core MODULE)
add_library(PulseWeaver::core ALIAS pulse-weaver-core)

target_sources(
  pulse-weaver-core
  PRIVATE pulse-weaver-core.cpp
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
