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

# Flash raw GBK lookup table into the dedicated data partition.
# NOTE: This partition is memory-mapped and read as a plain byte table by gb18030_conv.c,
# so it must NOT be a LittleFS image.
if(NOT IDF_TARGET STREQUAL "linux")
    set(GBK_TABLE_BIN "${CMAKE_CURRENT_SOURCE_DIR}/data/gbk_table.bin")
    if(EXISTS "${GBK_TABLE_BIN}")
        esptool_py_flash_to_partition(flash "gbk_table" "${GBK_TABLE_BIN}")
    else()
        message(WARNING "gbk_table.bin not found at ${GBK_TABLE_BIN}; GB18030/GBK conversion may fail")
    endif()
endif()
