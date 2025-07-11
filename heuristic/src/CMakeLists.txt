cmake_minimum_required(VERSION 3.10)
project(heuristic VERSION 1.0 LANGUAGES CXX)

set(CMAKE_C_COMPILER "/usr/bin/gcc-13")
set(CMAKE_CXX_COMPILER "/usr/bin/g++-13")

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# --- 定义可执行文件 ---
# 定义一个变量来存储你的可执行文件名，方便重用
set(EXECUTABLE_NAME heuristic)

# 主程序
# 收集指定目录下的所有源文件
file(GLOB_RECURSE SOURCES 
    ./[^.]*.cpp   ./[^.]*.hpp   # 排除隐藏文件（以.开头）
    ./[^.]*.c     ./[^.]*.h     # 可根据项目需要增删扩展名
)

add_executable(${EXECUTABLE_NAME} ${SOURCES})

# --- 链接库 ---
# find_package(MPI) 会提供一个导入的目标 MPI::MPI_CXX
# 这个目标包含了所有需要的头文件路径和库文件链接信息。
# target_link_libraries(${EXECUTABLE_NAME} PRIVATE MPI::MPI_CXX)

# 如果你的代码确实使用了多线程（比如 std::thread），再链接线程库
# find_package(Threads REQUIRED)
# target_link_libraries(${EXECUTABLE_NAME} PRIVATE Threads::Threads)
# --- 安装和输出 ---

# 打印出当前配置的编译模式，方便确认
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Executable will be named: ${EXECUTABLE_NAME}")