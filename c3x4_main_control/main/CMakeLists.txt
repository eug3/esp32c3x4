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

idf_component_register(SRCS "ui/home_screen.c"
                       "ui/file_browser_screen.c"
                       "ui/reader_screen_simple.c"
                       "ui/settings_screen_simple.c"
                       "ui/image_viewer_screen.c"
                       "ui/screen_manager.c"
                       "ui/display_engine.c"
                       "ui/font_renderer.c"
                       "ui/chinese_font_impl.c"
                       "ui/input_handler.c"
                       "ui/txt_reader.c"
                       "ui/epub_parser.c"
                       "ui/epub_zip.c"
                       "ui/epub_xml.c"
                       "ui/epub_html.c"
                       "main.c"
                       "DEV_Config.c"
                       "EPD_4in26.c"
                       "GUI_Paint.c"
                       "ImageData.c"
                       "Fonts/font12.c"
                       "Fonts/font16.c"
                       "Fonts/font20.c"
                       "Fonts/font24.c"
                       "Fonts/font8.c"
                       "Fonts/source_sans_pro_16.c"
                       ${EPUB_PARSER_SOURCES}
                       PRIV_REQUIRES spi_flash esp_driver_gpio esp_driver_spi driver esp_wifi nvs_flash bt fatfs littlefs esp_adc esp_timer
                       INCLUDE_DIRS "." "Fonts" "ui")

# Disable type-limits warning for GUI_Paint.c
set_source_files_properties(GUI_Paint.c PROPERTIES COMPILE_FLAGS "-Wno-type-limits")
