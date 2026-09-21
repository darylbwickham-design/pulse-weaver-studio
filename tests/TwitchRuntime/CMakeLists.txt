cmake_minimum_required(VERSION 3.28)
project(PulseTwitchRuntimeTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
# Compile the actual runtime class, with only its environment replaced by test
# doubles. This prevents a second implementation drifting away from production.
set(runtime "${CMAKE_CURRENT_SOURCE_DIR}/../../engine/obs-studio/plugins/pulse-weaver-core/pulse-weaver-core.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${runtime}")
file(READ "${runtime}" source)
string(FIND "${source}" "class TwitchRuntime final" begin)
string(FIND "${source}" "class KickRuntime final" end)
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${source}" ${begin} ${length} runtime_class)
string(REPLACE "private:" "public:" runtime_class "${runtime_class}")
string(REPLACE "QNetworkAccessManager network{this}" "FakeNetwork network{this}" runtime_class "${runtime_class}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/runtime-under-test.hpp" "${runtime_class}")
add_executable(PulseTwitchRuntimeTests main.cpp)
target_include_directories(PulseTwitchRuntimeTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(PulseTwitchRuntimeTests PRIVATE Qt6::Widgets Qt6::Network winhttp)
target_compile_definitions(PulseTwitchRuntimeTests PRIVATE NOMINMAX)
add_custom_command(TARGET PulseTwitchRuntimeTests POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE:Qt6::Gui>" "$<TARGET_FILE:Qt6::Widgets>" "$<TARGET_FILE:Qt6::Network>" "$<TARGET_FILE_DIR:PulseTwitchRuntimeTests>"
 COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PulseTwitchRuntimeTests>/platforms"
 COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QMinimalIntegrationPlugin>" "$<TARGET_FILE_DIR:PulseTwitchRuntimeTests>/platforms")
