#ifndef BLE_BARCODE_NIMBLE_H
#define BLE_BARCODE_NIMBLE_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize BLE barcode service using NimBLE stack
 *
 * @param device_name Name of the BLE device (will appear in BLE scans)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ble_init(const char *device_name);

/**
 * @brief Send barcode/UPC data over BLE
 *
 * @param barcode_data Null-terminated barcode string
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected, error code otherwise
 */
esp_err_t ble_send_barcode(const char *barcode_data);

/**
 * @brief Send cart_tracking cart tracking data over BLE
 *
 * @param cart_tracking_data Null-terminated cart_tracking data string
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected, error code otherwise
 */
esp_err_t ble_send_cart_tracking(const char *cart_tracking_data);

/**
 * @brief Send payment status (success or failure) over BLE
 *
 * @param payment_status Null-terminated payment status string
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected, error code otherwise
 */
esp_err_t ble_send_payment_status(const char *payment_status);

/**
 * @brief Send produce weight data over BLE
 *
 * @param weight_data Null-terminated weight data string
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected, error code otherwise
 */
esp_err_t ble_send_produce_weight(const char *weight_data);

/**
 * @brief Send item verification data over BLE
 *
 * @param verification_data Null-terminated item verification string
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected, error code otherwise
 */
esp_err_t ble_send_item_verification(const char *verification_data);

/**
 * @brief Send miscellaneous data over BLE
 *
 * @param misc_data Null-terminated miscellaneous data string
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected, error code otherwise
 */
esp_err_t ble_send_misc_data(const char *misc_data);

/**
 * @brief Check if a BLE client is connected
 *
 * @return true if connected, false otherwise
 */
bool ble_is_connected(void);

/**
 * @brief Register a callback function to handle received BLE data
 *
 * @param callback Function pointer to be called when data is received
 *                 Signature: void (*callback)(const char *data, uint16_t len)
 */
typedef void (*ble_rx_callback_t)(const char *data, uint16_t len);
void ble_register_rx_callback(ble_rx_callback_t callback);

/**
 * @brief Deinitialize BLE barcode service
 */
void ble_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_BARCODE_NIMBLE_H