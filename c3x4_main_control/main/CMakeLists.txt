idf_component_register(SRCS "ui/index_screen.c" "ui/file_browser.c" "ui/screen_manager.c" "lvgl_driver.c" "main.c" "DEV_Config.c" "EPD_4in26.c" "GUI_Paint.c" "ImageData.c"
                       "Fonts/font12.c" "Fonts/font16.c" "Fonts/font20.c" "Fonts/font24.c" "Fonts/font8.c"
                       PRIV_REQUIRES spi_flash esp_driver_gpio esp_driver_spi driver esp_wifi nvs_flash bt fatfs littlefs esp_adc lvgl__lvgl
                       INCLUDE_DIRS "." "Fonts" "ui")

# Disable type-limits warning for GUI_Paint.c
set_source_files_properties(GUI_Paint.c PROPERTIES COMPILE_FLAGS "-Wno-type-limits")
