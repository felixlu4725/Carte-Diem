#pragma once
#include "driver/uart.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uart_port_t uart_num;
    int tx_pin;
    int rx_pin;
    bool verbose;
    bool continuous_mode;
} barcode_t;

/**
 * @brief Initialize barcode scanner with UART configuration
 */
void barcode_init(barcode_t *scanner, uart_port_t uart_num, int tx_pin, int rx_pin, bool verbose);

/**
 * @brief Set scanner to manual trigger mode
 */
void barcode_set_manual_mode(barcode_t *scanner);

/**
 * @brief Set scanner to continuous scanning mode
 */
void barcode_set_continuous_mode(barcode_t *scanner);

/**
 * @brief Trigger a single scan in manual mode
 */
void barcode_trigger_scan(barcode_t *scanner);

/**
 * @brief Read a complete barcode line from UART buffer
 */
bool barcode_read_line(barcode_t *scanner, char *buf, size_t max_len);
