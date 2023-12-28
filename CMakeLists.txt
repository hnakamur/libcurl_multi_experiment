cmake_minimum_required(VERSION 3.20..3.27)
project(MyProject VERSION 0.0.0)

set(CMAKE_CXX_STANDARD
    20
    CACHE STRING "The C++ standard to compile with (default 20)"
)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(hardening)
add_compile_options(-flto -Wunused-parameter)
add_link_options(-flto)

enable_testing()
add_subdirectory(src)
# add_subdirectory(tests)

include(clang_format)
add_clang_format_target(clang_format_src src)
add_custom_target(clang_format DEPENDS clang_format_src)

include(cmake_format)
add_cmake_format_target_toplevel(cmake_format_toplevel)
add_cmake_format_target(cmake_format_cmake_dir cmake)
add_cmake_format_target(cmake_format_src_dir src)
add_custom_target(cmake_format DEPENDS cmake_format_toplevel cmake_format_cmake_dir cmake_format_src_dir)

# NOTE: Remove ALL below if you do not want to execute the formatting target every time.
add_custom_target(format ALL DEPENDS clang_format cmake_format)
