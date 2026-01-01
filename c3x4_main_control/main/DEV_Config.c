/*****************************************************************************
* | File      	:   DEV_Config.c
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface for ESP32
* | Info        :
*----------------
* |	This version:   ESP32 V1.0
* | Date        :   2024-12-26
******************************************************************************/
#include "DEV_Config.h"

static spi_device_handle_t spi_handle;
static bool spi_bus_initialized = false;  // 跟踪SPI总线是否已初始化

/**
 * GPIO read and write
**/
void DEV_Digital_Write(UWORD Pin, UBYTE Value) {
    gpio_set_level(Pin, Value);
}

UBYTE DEV_Digital_Read(UWORD Pin) {
    return gpio_get_level(Pin);
}

/**
 * SPI
**/
void DEV_SPI_WriteByte(UBYTE Value) {
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &Value,
    };
    spi_device_transmit(spi_handle, &trans);
}

void DEV_SPI_Write_nByte(uint8_t *pData, uint32_t Len) {
    spi_transaction_t trans = {
        .length = Len * 8,
        .tx_buffer = pData,
    };
    spi_device_transmit(spi_handle, &trans);
}

/**
 * GPIO Mode
**/
static void DEV_GPIO_Mode(UWORD Pin, UWORD Mode) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << Pin),
        .mode = Mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

/**
 * delay x ms
**/
void DEV_Delay_ms(UDOUBLE xms) {
    vTaskDelay(xms / portTICK_PERIOD_MS);
}

/**
 * Module Init
**/
UBYTE DEV_Module_Init(void) {
    // Initialize SPI bus only if not already initialized
    if (!spi_bus_initialized) {
        spi_bus_config_t buscfg = {
            .mosi_io_num = EPD_MOSI_PIN,
            .miso_io_num = -1,  // Not used
            .sclk_io_num = EPD_SCLK_PIN,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

        // Add SPI device
        spi_device_interface_config_t devcfg = {
            .clock_speed_hz = 10 * 1000 * 1000,  // 10MHz
            .mode = 0,  // SPI mode 0
            .spics_io_num = EPD_CS_PIN,
            .queue_size = 7,
        };
        ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle));
        
        spi_bus_initialized = true;
    }

    // Initialize GPIO (always do this as it's idempotent)
    DEV_GPIO_Mode(EPD_RST_PIN, GPIO_MODE_OUTPUT);
    DEV_GPIO_Mode(EPD_DC_PIN, GPIO_MODE_OUTPUT);
    DEV_GPIO_Mode(EPD_BUSY_PIN, GPIO_MODE_INPUT);

    return 0;
}

/**
 * Module Exit
**/
void DEV_Module_Exit(void) {
    ESP_ERROR_CHECK(spi_bus_remove_device(spi_handle));
    ESP_ERROR_CHECK(spi_bus_free(SPI2_HOST));
}