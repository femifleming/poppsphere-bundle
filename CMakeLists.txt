cmake_minimum_required(VERSION 3.22)
# The poppsphere bundle: openprocess, opensphere and Quell, built together.
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --target openprocess_All opensphere_All Quell_All
# On macOS the release is universal: add -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64".
# Each folder also builds on its own (cmake -S openprocess -B build-op).
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
project(PoppsphereBundle VERSION 1.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(JUCE)
add_subdirectory(openprocess)
add_subdirectory(quell)
