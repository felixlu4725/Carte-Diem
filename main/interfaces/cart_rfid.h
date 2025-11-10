#ifndef CART_RFID_SIMPLE_H
#define CART_RFID_SIMPLE_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of unique tags that can be tracked
 */
#define CART_RFID_MAX_TAGS 100

/**
 * @brief Structure to hold information about a single RFID tag
 */
typedef struct {
    char tag[64];              /**< Tag ID in hex string format */
    int rssi;                  /**< Signal strength in dBm */
} cart_rfid_tag_t;

/**
 * @brief Callback function type for scan complete events
 * 
 * @param tags Array of unique tags detected during scan
 * @param count Number of unique tags detected
 */
typedef void (*cart_rfid_callback_t)(const cart_rfid_tag_t *tags, int count);

/**
 * @brief Cart RFID reader handle
 */
typedef struct cart_rfid_reader cart_rfid_reader_t;

/**
 * @brief Initialize the cart RFID reader
 * 
 * @param uart_port UART port number
 * @param tx_pin TX pin number
 * @param rx_pin RX pin number
 * @param callback Function to call when scan completes (required)
 * @return Pointer to initialized reader handle, or NULL on failure
 */
cart_rfid_reader_t* cart_rfid_init(uart_port_t uart_port, 
                                    int tx_pin, 
                                    int rx_pin,
                                    cart_rfid_callback_t callback);

/**
 * @brief Deinitialize and free the cart RFID reader
 * 
 * @param reader Pointer to reader handle
 */
void cart_rfid_deinit(cart_rfid_reader_t *reader);

/**
 * @brief Start a non-blocking scan for RFID tags (300ms duration)
 * 
 * @param reader Pointer to reader handle
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already scanning
 */
esp_err_t cart_rfid_scan(cart_rfid_reader_t *reader);

/**
 * @brief Check if a scan is currently running
 * 
 * @param reader Pointer to reader handle
 * @return true if scan is running, false otherwise
 */
bool cart_rfid_is_scanning(cart_rfid_reader_t *reader);

#ifdef __cplusplus
}
#endif

#endif // CART_RFID_SIMPLE_H