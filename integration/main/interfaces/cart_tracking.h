#ifndef CART_TRACKING_H
#define CART_TRACKING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the file system (SPIFFS)
 */
void InitFileSystem(void);

/**
 * @brief Setup cart tracking UART interface
 */
void SetUpCartTracking(void);

/**
 * @brief Start a cart tracking session
 * Creates a new log file for tracking
 */
void startSession(void);

/**
 * @brief End a cart tracking session
 * Reads and logs the session data, then deletes the log file
 *
 * @param sendBLE If true, sends the session log via BLE (RFID characteristic).
 *                If false, just prints the log and deletes the file.
 */
void endSession(bool sendBLE);

/**
 * @brief Perform a burst read of cart tracking RFID data
 * This is the main function that reads RFID tags from the cart
 * tracking device and logs them to the session file
 */
void BurstRead_CartTracking(void);

/**
 * @brief Check if a cart tracking BLE transfer is currently in progress
 * Other BLE operations should avoid transmitting during this time
 * @return true if transfer is active, false otherwise
 */
bool is_cart_tracking_transfer_active(void);

#ifdef __cplusplus
}
#endif

#endif // CART_TRACKING_H
