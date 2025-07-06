cmake_minimum_required(VERSION 3.10)
project(ExactSolver VERSION 1.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_SOURCE_DIR}/../cmake")

# --- 关键部分：查找和配置 MPI ---
# 这是最重要的一步。CMake会寻找MPI环境。
# 它会自动将 CMAKE_CXX_COMPILER 设置为 mpic++。
# 你之前手动寻找 g++/clang++ 的代码块可以完全被这一步取代。
# find_package(MPI REQUIRED)
find_package(Threads REQUIRED)

find_package(GUROBI REQUIRED)

include_directories(${GUROBI_INCLUDE_DIRS})

message(STATUS "Found Gurobi include dirs: ${GUROBI_INCLUDE_DIRS}")
message(STATUS "Found Gurobi library: ${GUROBI_LIBRARY}")
message(STATUS "Found Gurobi C++ library: ${GUROBI_CXX_LIBRARY}")

# --- 定义可执行文件 ---
# 定义一个变量来存储你的可执行文件名，方便重用
set(EXECUTABLE_NAME ExactSolver)

# 主程序
# 收集指定目录下的所有源文件
file(GLOB_RECURSE SOURCES 
    ./[^.]*.cpp   ./[^.]*.hpp   # 排除隐藏文件（以.开头）
    ./[^.]*.c     ./[^.]*.h     # 可根据项目需要增删扩展名
)

add_executable(${EXECUTABLE_NAME} ${SOURCES})

set_target_properties(${EXECUTABLE_NAME} PROPERTIES
    CXX_STANDARD ${CMAKE_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
)

# --- 链接库 ---
# find_package(MPI) 会提供一个导入的目标 MPI::MPI_CXX
# 这个目标包含了所有需要的头文件路径和库文件链接信息。
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
# --- 安装和输出 ---

# 打印出当前配置的编译模式，方便确认
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Executable will be named: ${EXECUTABLE_NAME}")