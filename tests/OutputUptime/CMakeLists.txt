cmake_minimum_required(VERSION 3.28)
project(PulseOutputUptimeTests LANGUAGES CXX)
add_executable(PulseOutputUptimeTests main.cpp)
target_compile_features(PulseOutputUptimeTests PRIVATE cxx_std_17)
