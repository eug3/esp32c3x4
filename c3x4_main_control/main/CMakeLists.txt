# EPUB parser dependencies
set(EPUB_PARSER_SOURCES "")

# Try to find miniz component (ESP-IDF component)
if(EXISTS ${COMPONENT_DIR}/../miniz)
    list(APPEND EPUB_PARSER_SOURCES "${COMPONENT_DIR}/../miniz/miniz.c")
    include_directories("${COMPONENT_DIR}/../miniz")
endif()

# Try to find tinyxml2 component
if(EXISTS ${COMPONENT_DIR}/../tinyxml2)
    list(APPEND EPUB_PARSER_SOURCES "${COMPONENT_DIR}/../tinyxml2/tinyxml2.cpp")
    include_directories("${COMPONENT_DIR}/../tinyxml2")
endif()

idf_component_register(SRCS
                       "main.c"
                       "DEV_Config.c"
                       "EPD_4in26.c"
                       "GUI_Paint.c"
                       "ImageData.c"
                       "Fonts/font12.c"
                       "Fonts/source_sans_pro_16.c"
                       "Fonts/font16.c"
                       "Fonts/font20.c"
                       "Fonts/font24.c"
                       "Fonts/font8.c"

                       # Hand-drawn UI / screens
                       "ui/display_engine.c"
                       "ui/boot_animation.c"
                       "ui/boot_animation_frames.c"
                       "ui/screen_manager.c"
                       "ui/input_handler.c"
                       "ui/ui_region_manager.c"
                       "ui/home_screen.c"
                       "ui/settings_screen_simple.c"
                       "ui/file_browser_screen.c"
                       "ui/reader_screen_simple.c"
                       "ui/image_viewer_screen.c"
                       # Bluetooth (BLE API incompatible with IDF v5.5.2 - needs API migration)
                       # \"ui/ble_reader_screen.c\"
                       # \"ui/ble_manager.c\"
                       # \"ui/ble_book_protocol.c\"
                       # \"ui/ble_cache_manager.c\"

                       # XTEink font (no LVGL dependency)
                       "ui/xt_eink_font.c"
                       "ui/xt_eink_font_impl.c"
                       "ui/font_cache.c"
                       "ui/font_selector.c"
                       "ui/font_select_screen.c"

                       # Image helpers (JPEG/BMP/PNG support)
                       "lib/tjpgd/tjpgd.c"
                       "ui/jpeg_helper.c"
                       "ui/bmp_helper.c"
                       "ui/png_helper.cpp"

                       # PNG decoder and zlib (for PNG support)
                       "lib/pngdec/adler32.c"
                       "lib/pngdec/crc32.c"
                       "lib/pngdec/infback.c"
                       "lib/pngdec/inffast.c"
                       "lib/pngdec/inflate.c"
                       "lib/pngdec/inftrees.c"
                       "lib/pngdec/PNGdec.cpp"
                       "lib/pngdec/zutil.c"

                       # EPUB/TXT helpers (used by file browser/reader)
                       "ui/epub_parser.c"
                       "ui/epub_html.c"
                       "ui/epub_xml.c"
                       "ui/epub_zip.c"
                       "ui/epub_cache.c"
                       "ui/txt_reader.c"
                       "ui/gb18030_conv.c"

                       ${EPUB_PARSER_SOURCES}
                       PRIV_REQUIRES spi_flash esp_driver_gpio esp_driver_spi driver esp_wifi nvs_flash fatfs littlefs esp_adc esp_timer
                       INCLUDE_DIRS "." "Fonts" "ui" "lib/tjpgd" "lib/pngdec")

# Disable type-limits warning for GUI_Paint.c
set_source_files_properties(GUI_Paint.c PROPERTIES COMPILE_FLAGS "-Wno-type-limits")

# Disable warnings for PNGdec zlib files
set_source_files_properties(lib/pngdec/infback.c PROPERTIES COMPILE_FLAGS "-Wno-implicit-fallthrough")
set_source_files_properties(lib/pngdec/inflate.c PROPERTIES COMPILE_FLAGS "-Wno-implicit-fallthrough")
set_source_files_properties(lib/pngdec/zutil.c PROPERTIES COMPILE_FLAGS "-Wno-old-style-definition -Wno-error=old-style-definition")
