idf_component_register(SRCS "main.c" "DEV_Config.c" "EPD_4in26.c" "GUI_Paint.c" "ImageData.c"
                       PRIV_REQUIRES spi_flash esp_driver_gpio esp_driver_spi driver esp_wifi esp_http_server nvs_flash bt fatfs littlefs
                       INCLUDE_DIRS ".")

# Disable type-limits warning for GUI_Paint.c
set_source_files_properties(GUI_Paint.c PROPERTIES COMPILE_FLAGS "-Wno-type-limits")
