# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# 生成版本文件
execute_process(
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/generate_version.py ${CMAKE_CURRENT_SOURCE_DIR}/main/version.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE VERSION_RESULT
)

if(NOT VERSION_RESULT EQUAL 0)
    message(WARNING "Failed to generate version file")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(monster-c3x4)
