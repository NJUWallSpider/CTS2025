cmake_minimum_required(VERSION 3.10)
project(heuristic VERSION 1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# --- Define Executable ---
# Define a variable to store your executable name for reuse
set(EXECUTABLE_NAME Heuristic)

# Main Program
# Collect all source files in the specified directory
file(GLOB_RECURSE SOURCES 
    ./[^.]*.cpp   ./[^.]*.hpp   # Exclude hidden files (starting with .)
    ./[^.]*.c     ./[^.]*.h     # Add or remove extensions as needed by the project
)

add_executable(${EXECUTABLE_NAME} ${SOURCES})

# --- Link Libraries ---
# find_package(MPI) provides an imported target MPI::MPI_CXX
# This target contains all necessary header paths and library link information.
# target_link_libraries(${EXECUTABLE_NAME} PRIVATE MPI::MPI_CXX)

# If your code indeed uses multi-threading (e.g. std::thread), link threads library
# find_package(Threads REQUIRED)
# target_link_libraries(${EXECUTABLE_NAME} PRIVATE Threads::Threads)
# --- Install and Output ---

# Print current configuration build mode for confirmation
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Executable will be named: ${EXECUTABLE_NAME}")
