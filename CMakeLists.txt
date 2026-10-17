cmake_minimum_required(VERSION 3.24)

project(DJMixer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DJMIX_BUILD_APP "Build the JUCE desktop app" ON)
option(DJMIX_BUILD_TESTS "Build the Catch2 unit tests" ON)

include(cmake/get_cpm.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(app)

if(DJMIX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(app/Tests)
endif()
