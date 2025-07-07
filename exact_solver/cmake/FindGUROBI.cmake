find_path(GUROBI_INCLUDE_DIRS
    NAMES gurobi_c++.h gurobi_c.h
    HINTS ${GUROBI_DIR} $ENV{GUROBI_HOME} /Library/gurobi1202/macos_universal2 /opt/gurobi1202
    PATH_SUFFIXES include)

find_library(GUROBI_LIBRARY
    NAMES gurobi120 gurobi gurobi100 gurobi110
    HINTS ${GUROBI_DIR} $ENV{GUROBI_HOME} /Library/gurobi1202/macos_universal2 /opt/gurobi1202
    PATH_SUFFIXES lib)

# 明确查找 C++ 库
find_library(GUROBI_CXX_LIBRARY
    NAMES gurobi_c++ libgurobi_c++
    HINTS ${GUROBI_DIR} $ENV{GUROBI_HOME} /Library/gurobi1202/macos_universal2 /opt/gurobi1202
    PATH_SUFFIXES lib)

# Removed the hardcoded path as it was incorrect for the user's system.
# The HINTS should now correctly find the library if GUROBI_HOME is set.

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GUROBI DEFAULT_MSG GUROBI_INCLUDE_DIRS GUROBI_LIBRARY GUROBI_CXX_LIBRARY)

# 输出调试信息
message(STATUS "GUROBI_HOME environment variable: $ENV{GUROBI_HOME}")
message(STATUS "GUROBI_INCLUDE_DIRS: ${GUROBI_INCLUDE_DIRS}")
message(STATUS "GUROBI_LIBRARY: ${GUROBI_LIBRARY}")
message(STATUS "GUROBI_CXX_LIBRARY: ${GUROBI_CXX_LIBRARY}")
