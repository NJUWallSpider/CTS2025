cmake_minimum_required(VERSION 3.10)
project(ExactSolver VERSION 1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_SOURCE_DIR}/../cmake")

set(GUROBI_DIR $ENV{GUROBI_HOME} CACHE PATH "Path to Gurobi installation")
message(STATUS "Setting GUROBI_DIR to: ${GUROBI_DIR}")

# --- Critical section: Find and Configure MPI ---
# This is the most important step. CMake will look for the MPI environment.
# It will automatically set CMAKE_CXX_COMPILER to mpic++.
# Your previous manual search for g++/clang++ code block can be completely replaced by this step.
# find_package(MPI REQUIRED)
find_package(Threads REQUIRED)

find_package(GUROBI REQUIRED)

include_directories(${GUROBI_INCLUDE_DIRS})

message(STATUS "Found Gurobi include dirs: ${GUROBI_INCLUDE_DIRS}")
message(STATUS "Found Gurobi library: ${GUROBI_LIBRARY}")
message(STATUS "Found Gurobi C++ library: ${GUROBI_CXX_LIBRARY}")
message(STATUS "GUROBI_HOME (from ENV): $ENV{GUROBI_HOME}")

# --- Define Executable ---
# Define a variable to store your executable name for reuse
set(EXECUTABLE_NAME ExactSolver)

# Main Program
# Collect all source files in the specified directory
file(GLOB_RECURSE SOURCES 
    ./[^.]*.cpp   ./[^.]*.hpp   # Exclude hidden files (starting with .)
    ./[^.]*.c     ./[^.]*.h     # Add or remove extensions as needed by the project
)

add_executable(${EXECUTABLE_NAME} ${SOURCES})

set_target_properties(${EXECUTABLE_NAME} PROPERTIES
    CXX_STANDARD ${CMAKE_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
)

# --- Link Libraries ---
# find_package(MPI) provides an imported target MPI::MPI_CXX
# This target contains all necessary header paths and library link information.
target_link_libraries(${EXECUTABLE_NAME} PRIVATE 
    # MPI::MPI_CXX 
    Threads::Threads
    ${GUROBI_CXX_LIBRARY}
    ${GUROBI_LIBRARY}
    dl
)

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  include(FeatureSummary)
  feature_summary(WHAT ALL)
endif()
# --- Install and Output ---

# Print current configuration build mode for confirmation
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Executable will be named: ${EXECUTABLE_NAME}")
