cmake_minimum_required(VERSION 3.28...3.30)

add_library(pulse-weaver-sample-clock MODULE)
target_sources(pulse-weaver-sample-clock PRIVATE pulse-weaver-sample-clock.cpp)
target_link_libraries(pulse-weaver-sample-clock PRIVATE OBS::libobs OBS::frontend-api)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in pulse-weaver-sample-clock.rc)
  target_sources(pulse-weaver-sample-clock PRIVATE pulse-weaver-sample-clock.rc)
endif()

set_target_properties_obs(
  pulse-weaver-sample-clock
  PROPERTIES FOLDER plugins/pulse-weaver-sample-clock
             PREFIX ""
)
