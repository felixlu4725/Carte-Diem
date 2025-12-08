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
#define ITEM_RFID_MAX_TAGS 100

/**
 * @brief Structure to hold information about a single RFID tag
 */
typedef struct {
    char tag[64];              /**< Tag ID in hex string format */
    int rssi;                  /**< Signal strength in dBm */
} item_rfid_tag_t;

/**
 * @brief Callback function type for scan complete events
 * 
 * @param tags Array of unique tags detected during scan
 * @param count Number of unique tags detected
 */
typedef void (*item_rfid_callback_t)(const item_rfid_tag_t *tags, int count);

/**
 * @brief Item RFID reader handle
 */
typedef struct item_rfid_reader item_rfid_reader_t;

/**
 * @brief Initialize the item RFID reader
 */
item_rfid_reader_t* item_rfid_init(uart_port_t uart_port,
                                    int tx_pin,
                                    int rx_pin,
                                    item_rfid_callback_t callback);

/**
 * @brief Deinitialize and free the item RFID reader
 */
void item_rfid_deinit(item_rfid_reader_t *reader);

/**
 * @brief Start a non-blocking scan for RFID tags
 */
esp_err_t item_rfid_scan(item_rfid_reader_t *reader);

/**
 * @brief Check if a scan is currently running
 */
bool item_rfid_is_scanning(item_rfid_reader_t *reader);

#ifdef __cplusplus
}
#endif

#endif // CART_RFID_SIMPLE_H