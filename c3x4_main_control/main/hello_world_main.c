#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "DEV_Config.h"
#include "EPD_4in26.h"
#include "GUI_Paint.h"
#include "ImageData.h"

// Define button pin for sleep (using GPIO 9, which is available and not used by EPD)
#define SLEEP_BUTTON_PIN GPIO_NUM_9

// Define driver selection
#define TEST_DRIVER_SSD1677 1
#define TEST_DRIVER_GDEQ0426T82 2
#define TEST_DRIVER_SSD1681 3

#define CURRENT_DRIVER TEST_DRIVER_SSD1681  // Change this to test different drivers

void test_driver(int driver) {
    printf("Testing driver: %d\n", driver);
    switch (driver) {
        case TEST_DRIVER_SSD1677:
            printf("Using SSD1677 (EPD_4in26)\n");
            EPD_4in26_Init();
            printf("EPD_4in26_Init done\n");
            EPD_4in26_Clear();
            printf("EPD_4in26_Clear done\n");
            // Create and display black image
            UBYTE *Image;
            UWORD Imagesize = ((EPD_4in26_WIDTH % 8 == 0) ? (EPD_4in26_WIDTH / 8) : (EPD_4in26_WIDTH / 8 + 1)) * EPD_4in26_HEIGHT;
            if ((Image = (UBYTE *)malloc(Imagesize)) == NULL) {
                printf("Failed to apply for memory...\n");
                return;
            }
            memset(Image, 0x00, Imagesize);  // Black
            EPD_4in26_Display(Image);
            printf("EPD_4in26_Display (black) done\n");
            free(Image);
            EPD_4in26_Sleep();
            printf("EPD_4in26_Sleep done\n");
            break;
        case TEST_DRIVER_GDEQ0426T82:
            printf("Using GDEQ0426T82 (placeholder - need to implement)\n");
            // Placeholder: implement GDEQ0426T82 init and display
            break;
        case TEST_DRIVER_SSD1681:
            printf("Using SSD1681 (placeholder - implementing basic sequence)\n");
            // Basic SSD1681 initialization sequence (simplified)
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x12); // SWRESET
            vTaskDelay(10 / portTICK_PERIOD_MS);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x01); // Driver output control
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0xC7); // Height low
            DEV_SPI_WriteByte(0x00); // Height high
            DEV_SPI_WriteByte(0x01); // Other
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x11); // Data entry mode
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0x01); // Y increment, X increment
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x44); // Set RAM X address
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0x00);
            DEV_SPI_WriteByte(0x18);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x45); // Set RAM Y address
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0xC7);
            DEV_SPI_WriteByte(0x00);
            DEV_SPI_WriteByte(0x00);
            DEV_SPI_WriteByte(0x00);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x3C); // Border waveform
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0x05);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x18); // Temperature sensor
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0x80);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x4E); // Set RAM X counter
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0x00);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x4F); // Set RAM Y counter
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0xC7);
            DEV_SPI_WriteByte(0x00);
            
            printf("SSD1681 init done\n");
            
            // Clear screen (send white data)
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x24); // Write RAM
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            for (int i = 0; i < 5000; i++) { // Approximate size
                DEV_SPI_WriteByte(0xFF); // White
            }
            
            printf("SSD1681 clear done\n");
            
            // Display update
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x22); // Display update control
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0xF7);
            
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x20); // Master activation
            // Wait for BUSY
            while (DEV_Digital_Read(EPD_BUSY_PIN) == 1) {
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
            
            printf("SSD1681 display done\n");
            
            // Sleep
            DEV_Digital_Write(EPD_DC_PIN, 0); // Command
            DEV_SPI_WriteByte(0x10); // Deep sleep
            DEV_Digital_Write(EPD_DC_PIN, 1); // Data
            DEV_SPI_WriteByte(0x01);
            
            printf("SSD1681 sleep done\n");
            break;
        default:
            printf("Unknown driver\n");
            break;
    }
}

// Button press detection and deep sleep function
void check_button_and_sleep(void) {
    // Configure button pin as input with pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SLEEP_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    printf("Press button on GPIO %d to enter deep sleep...\n", SLEEP_BUTTON_PIN);
    printf("Double-click button on GPIO %d to re-test EPD display...\n", SLEEP_BUTTON_PIN);

    int last_button_state = 1;  // Button not pressed (high with pull-up)
    int press_count = 0;
    TickType_t last_press_time = 0;
    const TickType_t DOUBLE_CLICK_TIME = 500 / portTICK_PERIOD_MS;  // 500ms window for double click

    while (1) {
        int button_state = gpio_get_level(SLEEP_BUTTON_PIN);

        // Detect button press (falling edge)
        if (last_button_state == 1 && button_state == 0) {
            TickType_t current_time = xTaskGetTickCount();

            // Check if this is within double-click time window
            if ((current_time - last_press_time) < DOUBLE_CLICK_TIME) {
                press_count++;
            } else {
                press_count = 1;
            }

            last_press_time = current_time;

            // Debounce delay
            vTaskDelay(50 / portTICK_PERIOD_MS);

            // Wait for button release
            while (gpio_get_level(SLEEP_BUTTON_PIN) == 0) {
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }

            // Check for double click after button release
            if (press_count >= 2) {
                printf("Double-click detected! Re-testing EPD display...\n");
                // Re-run EPD test
                test_driver(CURRENT_DRIVER);
                printf("EPD re-test completed. Press button for sleep or double-click for re-test.\n");
                press_count = 0;
            } else {
                // Single click - wait a bit to see if it's part of a double click
                vTaskDelay(DOUBLE_CLICK_TIME - (xTaskGetTickCount() - last_press_time));

                if (press_count == 1) {
                    printf("Single click detected! Entering deep sleep in 1 second...\n");
                    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Final confirmation delay

                    printf("Entering deep sleep now. Press reset button to wake up.\n");

                    // For ESP32-C3, ext0 wakeup may not be available, so we'll just enter deep sleep
                    // User needs to press reset button to wake up
                    esp_deep_sleep_start();
                }
            }
        }

        last_button_state = button_state;
        vTaskDelay(10 / portTICK_PERIOD_MS);  // Poll every 10ms for better responsiveness
    }
}

void app_main(void)
{
    printf("Hello World from ESP32!\n");

    // Initialize SPI and e-Paper
    printf("Initializing DEV_Module...\n");
    DEV_Module_Init();
    printf("DEV_Module_Init done\n");

    // Test SPI communication
    printf("Testing SPI communication...\n");
    DEV_SPI_WriteByte(0x00);  // Send a test byte
    printf("SPI test done\n");

    // Low-level hardware self-check: pulse RST, toggle CS, send SPI pattern, read BUSY
    printf("Starting low-level hardware self-check...\n");
    // Pulse RST
    DEV_Digital_Write(EPD_RST_PIN, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    DEV_Digital_Write(EPD_RST_PIN, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Toggle CS and send pattern
    for (int pass = 0; pass < 3; pass++) {
        printf("Self-check pass %d: CS LOW, send 0xAA,0x55 then CS HIGH\n", pass+1);
        DEV_Digital_Write(EPD_CS_PIN, 0);
        DEV_SPI_WriteByte(0xAA);
        DEV_SPI_WriteByte(0x55);
        DEV_Digital_Write(EPD_CS_PIN, 1);
        // Read BUSY 10 times
        for (int i = 0; i < 10; i++) {
            int busy = DEV_Digital_Read(EPD_BUSY_PIN);
            printf("BUSY read %d: %d\n", i, busy);
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }
    printf("Low-level self-check finished.\n");

    // Test different drivers
    test_driver(CURRENT_DRIVER);

    printf("Test completed!\n");

    // Start button monitoring for deep sleep
    check_button_and_sleep();
}
