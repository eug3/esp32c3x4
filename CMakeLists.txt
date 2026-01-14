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

# 添加自定义烧录目标用于数据分区
# 使用方法：
#   idf.py build               # 只编译固件
#   idf.py flash               # 只烧录固件
#   idf.py flash-data          # 烧录数据分区（字体+GBK表）
#   idf.py flash-all           # 烧录固件+数据分区

if(NOT IDF_TARGET STREQUAL "linux")
    # 数据文件路径
    set(GBK_TABLE_BIN "${CMAKE_CURRENT_SOURCE_DIR}/data/gbk_table.bin")
    set(FONT_BIN "${CMAKE_CURRENT_SOURCE_DIR}/data/msyh-14.25pt.19×25.bin")
    
    # 分区偏移（必须与 partitions.csv 一致）
    set(GBK_TABLE_OFFSET "0xF10000")
    set(FONT_DATA_OFFSET "0xA10000")
    
    # 创建自定义烧录目标
    if(EXISTS "${GBK_TABLE_BIN}" OR EXISTS "${FONT_BIN}")
        add_custom_target(flash-data
            COMMAND echo "=== Flashing data partitions ==="
            COMMAND ${CMAKE_COMMAND} -E echo "GBK Table: ${GBK_TABLE_BIN}"
            COMMAND ${CMAKE_COMMAND} -E echo "Font Data: ${FONT_BIN}"
            COMMAND ${CMAKE_COMMAND} -E echo ""
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Flash data partitions (font + GBK table)"
        )
        
        # 烧录 GBK 表
        if(EXISTS "${GBK_TABLE_BIN}")
            add_custom_command(TARGET flash-data POST_BUILD
                COMMAND esptool.py write_flash ${GBK_TABLE_OFFSET} ${GBK_TABLE_BIN}
                COMMENT "Flashing GBK table to ${GBK_TABLE_OFFSET}"
            )
        endif()
        
        # 烧录字体
        if(EXISTS "${FONT_BIN}")
            add_custom_command(TARGET flash-data POST_BUILD
                COMMAND esptool.py write_flash ${FONT_DATA_OFFSET} ${FONT_BIN}
                COMMENT "Flashing font data to ${FONT_DATA_OFFSET}"
            )
        endif()
        
        message(STATUS "Data files found:")
        if(EXISTS "${GBK_TABLE_BIN}")
            message(STATUS "  - GBK table: ${GBK_TABLE_BIN}")
        endif()
        if(EXISTS "${FONT_BIN}")
            message(STATUS "  - Font data: ${FONT_BIN}")
        endif()
        message(STATUS "Use 'idf.py flash-data' to flash data partitions")
    endif()
endif()
