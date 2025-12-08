#include "barcode.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "BARCODE";

// Commands
static const uint8_t MANUAL_MODE_CMD[9]     = {0x7E,0x00,0x08,0x01,0x00,0x00,0xD5,0xAB,0xCD};
static const uint8_t CONTINUOUS_MODE_CMD[9] = {0x7E,0x00,0x08,0x01,0x00,0x00,0xD6,0xAB,0xCD};
static const uint8_t TRIGGER_CMD[9]         = {0x7E,0x00,0x08,0x01,0x00,0x02,0x01,0xAB,0xCD};

/**
 * @brief Initialize barcode scanner with UART configuration
 */
void barcode_init(barcode_t *scanner, uart_port_t uart_num, int tx_pin, int rx_pin, bool verbose) {
    scanner->uart_num = uart_num;
    scanner->tx_pin = tx_pin;
    scanner->rx_pin = rx_pin;
    scanner->verbose = verbose;
    scanner->continuous_mode = false;

    uart_config_t cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret;

    ret = uart_driver_install(uart_num, 2048, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        if (verbose) ESP_LOGE(TAG, "Failed to install UART driver: %d", ret);
        return;
    }

    ret = uart_param_config(uart_num, &cfg);
    if (ret != ESP_OK) {
        if (verbose) ESP_LOGE(TAG, "Failed to configure UART params: %d", ret);
        return;
    }

    ret = uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        if (verbose) ESP_LOGE(TAG, "Failed to set UART pins: %d", ret);
        return;
    }

    if (scanner->verbose)
        ESP_LOGI(TAG, "✓ Barcode scanner initialized (UART%d)", uart_num);

    barcode_set_manual_mode(scanner);
}

/**
 * @brief Send command to barcode scanner via UART
 */
static void send_cmd(barcode_t *scanner, const uint8_t *cmd, size_t len) {
    uart_write_bytes(scanner->uart_num, (const char*)cmd, len);
    esp_err_t ret = uart_wait_tx_done(scanner->uart_num, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        if (scanner->verbose)
            ESP_LOGW(TAG, "UART TX timeout (continuing anyway)");
    }
}

/**
 * @brief Set scanner to manual trigger mode
 */
void barcode_set_manual_mode(barcode_t *scanner) {
    send_cmd(scanner, MANUAL_MODE_CMD, sizeof(MANUAL_MODE_CMD));
    scanner->continuous_mode = false;
    if (scanner->verbose)
        ESP_LOGI(TAG, "Manual mode ON");
}

/**
 * @brief Set scanner to continuous scanning mode
 */
void barcode_set_continuous_mode(barcode_t *scanner) {
    send_cmd(scanner, CONTINUOUS_MODE_CMD, sizeof(CONTINUOUS_MODE_CMD));
    scanner->continuous_mode = true;
    if (scanner->verbose)
        ESP_LOGI(TAG, "Continuous mode ON");
}

/**
 * @brief Trigger a single scan in manual mode
 */
void barcode_trigger_scan(barcode_t *scanner) {
    send_cmd(scanner, TRIGGER_CMD, sizeof(TRIGGER_CMD));
    if (scanner->verbose)
        ESP_LOGI(TAG, "Triggered scan");
}

/**
 * @brief Read a complete barcode line from UART buffer
 */
bool barcode_read_line(barcode_t *scanner, char *buf, size_t max_len) {
    uint8_t ch;
    size_t idx = 0;

    while (1) {
        int n = uart_read_bytes(scanner->uart_num, &ch, 1, pdMS_TO_TICKS(20));
        if (n <= 0) break;
        if (ch == '\r' || ch == '\n') {
            if (idx > 0) {
                buf[idx] = '\0';
                if (scanner->verbose) ESP_LOGI(TAG, "Read: %s", buf);
                return true;
            }
        } else if (idx < max_len - 1) {
            buf[idx++] = ch;
        }
    }
    return false;
}