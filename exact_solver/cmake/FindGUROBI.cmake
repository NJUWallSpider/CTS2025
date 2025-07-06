find_path(GUROBI_INCLUDE_DIRS
    NAMES gurobi_c.h
    HINTS ${GUROBI_DIR} $ENV{GUROBI_HOME} /opt/gurobi1202
    PATH_SUFFIXES include)

find_library(GUROBI_LIBRARY
    NAMES gurobi120 gurobi gurobi100 gurobi110
    HINTS ${GUROBI_DIR} $ENV{GUROBI_HOME} /opt/gurobi1202
    PATH_SUFFIXES lib)

# 明确查找 C++ 库
find_library(GUROBI_CXX_LIBRARY
    NAMES gurobi_c++
    HINTS ${GUROBI_DIR} $ENV{GUROBI_HOME} /opt/gurobi1202
    PATH_SUFFIXES lib)
set(GUROBI_CXX_DEBUG_LIBRARY ${GUROBI_CXX_LIBRARY})

# 如果找不到 C++ 库，则尝试直接指定
if(NOT GUROBI_CXX_LIBRARY)
    if(EXISTS "/opt/gurobi1202/linux64/lib/libgurobi_c++.a")
        set(GUROBI_CXX_LIBRARY "/opt/gurobi1202/linux64/lib/libgurobi_c++.a")
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GUROBI DEFAULT_MSG GUROBI_LIBRARY GUROBI_CXX_LIBRARY)

# 输出调试信息
message(STATUS "GUROBI_INCLUDE_DIRS: ${GUROBI_INCLUDE_DIRS}")
message(STATUS "GUROBI_LIBRARY: ${GUROBI_LIBRARY}")
message(STATUS "GUROBI_CXX_LIBRARY: ${GUROBI_CXX_LIBRARY}")
