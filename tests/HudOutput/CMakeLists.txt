cmake_minimum_required(VERSION 3.28)
project(PulseHudTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
get_filename_component(REPO "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(OBS_ROOT "${REPO}/engine/obs-studio")
set(OBS_BUILD "" CACHE PATH "Existing ABI-compatible OBS build")
add_executable(PulseHudTests main.cpp "${OBS_ROOT}/frontend/widgets/PulseHudOutput.cpp")
target_compile_definitions(PulseHudTests PRIVATE PULSE_HUD_TESTS NOMINMAX WIN32_LEAN_AND_MEAN)
# libobs headers intentionally use anonymous SIMD structs; the fixture's
# legacy data-path API matches the existing native regression harnesses.
target_compile_options(PulseHudTests PRIVATE /utf-8 /W4 /WX /wd4201 /wd4996)
target_include_directories(PulseHudTests PRIVATE "${OBS_ROOT}/libobs" "${OBS_BUILD}/config")
target_link_libraries(PulseHudTests PRIVATE Qt6::Widgets "${OBS_BUILD}/libobs/RelWithDebInfo/obs.lib" d3d11 dxgi dcomp)
add_custom_command(TARGET PulseHudTests POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
  "${OBS_BUILD}/rundir/RelWithDebInfo/bin/64bit/obs.dll" "$<TARGET_FILE_DIR:PulseHudTests>/obs.dll")
