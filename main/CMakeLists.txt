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

                       # UI Core - display, input, screen management
                       "ui/core/display_engine.c"
                       "ui/core/screen_manager.c"
                       "ui/core/input_handler.c"
                       "ui/core/ui_region_manager.c"
                       "ui/core/paginated_menu.c"
                       "ui/reading_history.c"

                       # UI Screens
                       "ui/screens/home_screen.c"
                       "ui/screens/settings_screen.c"
                       "ui/screens/file_browser_screen.c"
                       "ui/screens/reader_screen.c"
                       "ui/screens/image_viewer_screen.c"
                       "ui/screens/wallpaper_screen.c"
                       "ui/screens/font_select_screen.c"
                       "ui/screens/boot_screen.c"
                       "ui/screens/ble_reader_screen.c"

                       # UI Boot animation
                       "ui/boot/boot_animation.c"
                       "ui/boot/boot_animation_frames.c"

                       # UI Wallpaper
                       "ui/wallpaper/wallpaper_manager.c"

                       # UI Fonts
                       "ui/fonts/xt_eink_font.c"
                       "ui/fonts/xt_eink_font_impl.c"
                       "ui/fonts/font_cache.c"
                       "ui/fonts/font_selector.c"
                       "ui/fonts/font_partition.c"

                       # UI Image helpers
                       "ui/image/jpeg_helper.c"
                       "ui/image/bmp_helper.c"
                       "ui/image/png_helper.cpp"

                       # Image libraries (tjpgd, PNGdec + bundled zlib)
                       "lib/tjpgd/tjpgd.c"
                       "lib/pngdec/PNGdec.cpp"
                       "lib/pngdec/adler32.c"
                       "lib/pngdec/crc32.c"
                       "lib/pngdec/infback.c"
                       "lib/pngdec/inffast.c"
                       "lib/pngdec/inftrees.c"
                       "lib/pngdec/inflate.c"
                       "lib/pngdec/zutil.c"

                       # XML parser
                       "lib/tinyxml2/tinyxml2.cpp"

                       # EPUB/TXT readers
                       "ui/epub/epub_parser.c"
                       "ui/epub/epub_html.c"
                       "ui/epub/epub_xml.cpp"
                       "ui/epub/epub_zip.c"
                       "ui/epub/epub_cache.c"
                       "ui/epub/epub_precache.c"
                       "ui/txt/txt_reader.c"
                       "ui/txt/gb18030_conv.c"

                       # Bluetooth (NimBLE on ESP-IDF 6.1)
                       "ui/ble/ble_manager.c"
                       "ui/ble/ble_book_protocol.c"
                       "ui/ble/ble_cache_manager.c"

                       "power_manager.c"
                       ${EPUB_PARSER_SOURCES}
                       PRIV_REQUIRES spi_flash esp_driver_gpio esp_driver_spi driver esp_wifi nvs_flash fatfs littlefs esp_adc esp_timer vfs bt
                       INCLUDE_DIRS "." "Fonts" "ui" "ui/core" "ui/screens" "ui/boot" "ui/fonts" "ui/epub" "ui/txt" "ui/image" "ui/wallpaper" "ui/ble" "lib/tjpgd" "lib/tinyxml2"
                       PRIV_INCLUDE_DIRS "lib/pngdec")

# Disable type-limits warning for GUI_Paint.c
set_source_files_properties(GUI_Paint.c PROPERTIES COMPILE_FLAGS "-Wno-type-limits")

# Disable warnings for PNGdec zlib files
set_source_files_properties(lib/pngdec/infback.c PROPERTIES COMPILE_FLAGS "-Wno-implicit-fallthrough")
set_source_files_properties(lib/pngdec/inflate.c PROPERTIES COMPILE_FLAGS "-Wno-implicit-fallthrough")
set_source_files_properties(lib/pngdec/zutil.c PROPERTIES COMPILE_FLAGS "-Wno-old-style-definition -Wno-error=old-style-definition")
