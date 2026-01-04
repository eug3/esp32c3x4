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
                       "ui/screen_manager.c"
                       "ui/input_handler.c"
                       "ui/ui_region_manager.c"
                       "ui/home_screen.c"
                       "ui/settings_screen_simple.c"
                       "ui/file_browser_screen.c"
                       "ui/reader_screen_simple.c"
                       "ui/image_viewer_screen.c"

                       # XTEink font (no LVGL dependency)
                       "ui/xt_eink_font.c"
                       "ui/xt_eink_font_impl.c"


                       # EPUB/TXT helpers (used by file browser/reader)
                       "ui/epub_parser.c"
                       "ui/epub_html.c"
                       "ui/epub_xml.c"
                       "ui/epub_zip.c"
                       "ui/txt_reader.c"

                       ${EPUB_PARSER_SOURCES}
                       PRIV_REQUIRES spi_flash esp_driver_gpio esp_driver_spi driver esp_wifi nvs_flash fatfs littlefs esp_adc esp_timer
                       INCLUDE_DIRS "." "Fonts" "ui")

# Disable type-limits warning for GUI_Paint.c
set_source_files_properties(GUI_Paint.c PROPERTIES COMPILE_FLAGS "-Wno-type-limits")
