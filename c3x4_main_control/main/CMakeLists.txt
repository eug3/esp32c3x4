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

idf_component_register(SRCS "ui/index_screen.c" "ui/file_browser.c" "ui/screen_manager.c" "ui/settings_screen.c" "ui/font_loader.c" "ui/font_manager.c" "ui/font_stream.c" "ui/reader_screen.c" "ui/txt_reader.c" "ui/epub_parser.c" "ui/image_browser.c" "ui/chinese_font.c" "ui/builtin_chinese_font.c" "ui/epub_zip.c" "ui/epub_xml.c" "ui/epub_html.c" "lvgl_driver.c" "main.c" "DEV_Config.c" "EPD_4in26.c" "GUI_Paint.c" "ImageData.c"
                       "Fonts/font12.c" "Fonts/font16.c" "Fonts/font20.c" "Fonts/font24.c" "Fonts/font8.c"
                       ${EPUB_PARSER_SOURCES}
                       PRIV_REQUIRES spi_flash esp_driver_gpio esp_driver_spi driver esp_wifi nvs_flash bt fatfs littlefs esp_adc lvgl__lvgl
                       INCLUDE_DIRS "." "Fonts" "ui"
                       EMBED_FILES "ui/chinese_font.bin")

# Disable type-limits warning for GUI_Paint.c
set_source_files_properties(GUI_Paint.c PROPERTIES COMPILE_FLAGS "-Wno-type-limits")
