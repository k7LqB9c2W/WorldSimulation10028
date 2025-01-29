cmake_minimum_required(VERSION 3.10)
project(CountrySimulator)

# Set C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SFML
find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)

# Include directories
include_directories(include)

# Source files
file(GLOB SOURCES "/*.cpp")

# Create executable
add_executable(CountrySimulator ${SOURCES})

# Link SFML libraries
target_link_libraries(CountrySimulator sfml-graphics sfml-window sfml-system)

