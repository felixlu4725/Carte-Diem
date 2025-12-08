#ifndef MFRC522_H
#define MFRC522_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MFRC522_MAX_UID_LEN 10

// SPI command construction (shifting is IMPORTANT and CRITICAL)
#define MFRC522_READ_CMD(addr)  (0x80 | ((addr) << 1))
#define MFRC522_WRITE_CMD(addr) (((addr) << 1) & 0x7E)

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t cs_pin;
    gpio_num_t rst_pin;
    uint8_t uid[MFRC522_MAX_UID_LEN];
    uint8_t uid_len;
} mfrc522_t;

/**
 * @brief Initialize MFRC522 RFID reader over SPI
 */
esp_err_t mfrc522_init(mfrc522_t *dev, spi_host_device_t host,
                       gpio_num_t miso, gpio_num_t mosi, gpio_num_t sck,
                       gpio_num_t cs, gpio_num_t rst);

/**
 * @brief Read UID from RFID card in proximity
 */
esp_err_t mfrc522_read_uid(mfrc522_t *dev, uint8_t *uid, uint8_t *uid_len);

/**
 * @brief Perform soft reset of MFRC522 chip
 */
void mfrc522_reset(mfrc522_t *dev);

#ifdef __cplusplus
}
#endif

#endif // MFRC522_H
