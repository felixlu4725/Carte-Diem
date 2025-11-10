#include "cart_rfid.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "CART_RFID";

#define SCAN_DURATION_MS 300
#define BURST_GAP_MS 100
#define MAX_BURST_TAGS 20

/**
 * @brief RFID reader start command
 */
static const uint8_t RFID_START_CMD[] = {0x43, 0x4D, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00};

/**
 * @brief Internal structure for the cart RFID reader
 */
struct cart_rfid_reader {
    uart_port_t uart_port;
    cart_rfid_callback_t callback;
    
    cart_rfid_tag_t burst_tags[MAX_BURST_TAGS];
    int burst_tag_count;
    
    cart_rfid_tag_t unique_tags[CART_RFID_MAX_TAGS];
    int unique_tag_count;
    
    uint8_t rx_buffer[64];
    int buffer_index;
    bool collecting;
    unsigned long last_frame_time;
    
    TaskHandle_t scan_task_handle;
    SemaphoreHandle_t mutex;
    bool is_scanning;
};

/**
 * @brief Get current time in milliseconds
 */
static inline unsigned long cart_rfid_millis(void) {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

/**
 * @brief Read a single byte from UART with timeout
 */
static inline int cart_rfid_read_byte(cart_rfid_reader_t *reader, uint8_t *byte_out) {
    return uart_read_bytes(reader->uart_port, byte_out, 1, pdMS_TO_TICKS(20));
}

/**
 * @brief Check if a tag already exists in the unique tags list
 */
static bool cart_rfid_tag_exists(cart_rfid_reader_t *reader, const char *tag) {
    for (int i = 0; i < reader->unique_tag_count; i++) {
        if (strcmp(reader->unique_tags[i].tag, tag) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Process burst of tags and add to unique list
 */
static void cart_rfid_process_burst(cart_rfid_reader_t *reader) {
    if (reader->burst_tag_count == 0) return;
    
    for (int i = 0; i < reader->burst_tag_count; i++) {
        if (!cart_rfid_tag_exists(reader, reader->burst_tags[i].tag) &&
            reader->unique_tag_count < CART_RFID_MAX_TAGS) {
            reader->unique_tags[reader->unique_tag_count++] = reader->burst_tags[i];
        }
    }
    
    reader->burst_tag_count = 0;
}

/**
 * @brief Process a complete RFID frame
 */
static void cart_rfid_process_frame(cart_rfid_reader_t *reader) {
    unsigned long current_time = cart_rfid_millis();
    
    uint8_t tag_len = reader->rx_buffer[9];
    uint8_t opt_ctrl = reader->rx_buffer[10];
    
    // Convert tag bytes to hex string
    char tag_hex[64] = {0};
    for (int i = 0; i < tag_len && i < 32; i++) {
        char tmp[4];
        snprintf(tmp, sizeof(tmp), "%02X", reader->rx_buffer[11 + i]);
        strcat(tag_hex, tmp);
    }
    
    // Extract RSSI if present
    uint8_t extra = (opt_ctrl & 0x40) ? 1 : 0;
    int rssi = extra ? (int)reader->rx_buffer[11 + tag_len + 1] : -999;
    
    // Add to burst if room available
    if (reader->burst_tag_count < MAX_BURST_TAGS) {
        strncpy(reader->burst_tags[reader->burst_tag_count].tag, tag_hex, 
                sizeof(reader->burst_tags[reader->burst_tag_count].tag) - 1);
        reader->burst_tags[reader->burst_tag_count].rssi = rssi;
        reader->burst_tag_count++;
    }
    
    reader->last_frame_time = current_time;
}

/**
 * @brief Main scanning logic
 */
static void cart_rfid_scan_internal(cart_rfid_reader_t *reader) {
    unsigned long start_time = cart_rfid_millis();
    
    // Reset state
    reader->burst_tag_count = 0;
    reader->unique_tag_count = 0;
    reader->collecting = false;
    reader->buffer_index = 0;
    
    // Send start command
    uart_write_bytes(reader->uart_port, (const char *)RFID_START_CMD, sizeof(RFID_START_CMD));
    vTaskDelay(pdMS_TO_TICKS(10));
    
    uint8_t byte = 0;
    
    while ((cart_rfid_millis() - start_time) < SCAN_DURATION_MS) {
        int read_bytes = cart_rfid_read_byte(reader, &byte);
        
        // Handle burst timeout
        if (read_bytes <= 0) {
            if (reader->burst_tag_count > 0 &&
                (reader->burst_tag_count >= MAX_BURST_TAGS ||
                 (cart_rfid_millis() - reader->last_frame_time) > BURST_GAP_MS)) {
                cart_rfid_process_burst(reader);
            }
            continue;
        }
        
        // Start collecting frame
        if (!reader->collecting) {
            if (byte == 0x43) { // Start byte
                reader->rx_buffer[0] = byte;
                reader->buffer_index = 1;
                reader->collecting = true;
            }
            continue;
        }
        
        reader->rx_buffer[reader->buffer_index++] = byte;
        
        // Validate second byte
        if (reader->buffer_index == 2 && reader->rx_buffer[1] != 0x4D) {
            reader->collecting = false;
            reader->buffer_index = 0;
            continue;
        }
        
        // Buffer overflow check
        if (reader->buffer_index >= sizeof(reader->rx_buffer)) {
            reader->collecting = false;
            reader->buffer_index = 0;
            continue;
        }
        
        // Check for complete frame
        if (reader->buffer_index >= 11) {
            uint8_t tag_len = reader->rx_buffer[9];
            uint8_t opt_ctrl = reader->rx_buffer[10];
            uint8_t extra = (opt_ctrl & 0x40) ? 1 : 0;
            int expected_len = 8 + 3 + tag_len + extra + 1;
            
            if (reader->buffer_index >= expected_len) {
                cart_rfid_process_frame(reader);
                reader->collecting = false;
                reader->buffer_index = 0;
            }
        }
    }
    
    // Process any remaining burst
    cart_rfid_process_burst(reader);
    
    ESP_LOGI(TAG, "Scan complete: %d unique tags found", reader->unique_tag_count);
}

/**
 * @brief Scan task for non-blocking operation
 */
static void cart_rfid_scan_task(void *arg) {
    cart_rfid_reader_t *reader = (cart_rfid_reader_t *)arg;
    
    // Perform scan
    cart_rfid_scan_internal(reader);
    
    // Call callback with results
    if (reader->callback) {
        reader->callback(reader->unique_tags, reader->unique_tag_count);
    }
    
    // Mark as not scanning
    xSemaphoreTake(reader->mutex, portMAX_DELAY);
    reader->is_scanning = false;
    reader->scan_task_handle = NULL;
    xSemaphoreGive(reader->mutex);
    
    vTaskDelete(NULL);
}

// -------------------------- Public API Implementation --------------------------

cart_rfid_reader_t* cart_rfid_init(uart_port_t uart_port, 
                                    int tx_pin, 
                                    int rx_pin,
                                    cart_rfid_callback_t callback) {
    if (!callback) {
        ESP_LOGE(TAG, "Callback is required");
        return NULL;
    }
    
    cart_rfid_reader_t *reader = calloc(1, sizeof(cart_rfid_reader_t));
    if (!reader) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return NULL;
    }
    
    reader->uart_port = uart_port;
    reader->callback = callback;
    
    // Initialize UART
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    
    esp_err_t err = uart_driver_install(uart_port, 2048, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver");
        free(reader);
        return NULL;
    }
    
    err = uart_param_config(uart_port, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART");
        uart_driver_delete(uart_port);
        free(reader);
        return NULL;
    }
    
    err = uart_set_pin(uart_port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins");
        uart_driver_delete(uart_port);
        free(reader);
        return NULL;
    }
    
    // Create mutex
    reader->mutex = xSemaphoreCreateMutex();
    if (!reader->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        uart_driver_delete(uart_port);
        free(reader);
        return NULL;
    }
    
    ESP_LOGI(TAG, "Cart RFID initialized (Port=%d, TX=%d, RX=%d, Scan=300ms)",
             uart_port, tx_pin, rx_pin);
    
    return reader;
}

void cart_rfid_deinit(cart_rfid_reader_t *reader) {
    if (!reader) return;
    
    // Stop any ongoing scan
    if (reader->scan_task_handle) {
        vTaskDelete(reader->scan_task_handle);
    }
    
    if (reader->mutex) {
        vSemaphoreDelete(reader->mutex);
    }
    
    uart_driver_delete(reader->uart_port);
    free(reader);
    
    ESP_LOGI(TAG, "Cart RFID deinitialized");
}

esp_err_t cart_rfid_scan(cart_rfid_reader_t *reader) {
    if (!reader) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(reader->mutex, portMAX_DELAY);
    
    if (reader->is_scanning) {
        xSemaphoreGive(reader->mutex);
        ESP_LOGW(TAG, "Scan already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    reader->is_scanning = true;
    xSemaphoreGive(reader->mutex);
    
    BaseType_t ret = xTaskCreate(cart_rfid_scan_task, "cart_rfid_scan",
                                  4096, reader, 5, &reader->scan_task_handle);
    
    if (ret != pdPASS) {
        xSemaphoreTake(reader->mutex, portMAX_DELAY);
        reader->is_scanning = false;
        xSemaphoreGive(reader->mutex);
        ESP_LOGE(TAG, "Failed to create scan task");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

bool cart_rfid_is_scanning(cart_rfid_reader_t *reader) {
    if (!reader) return false;
    
    xSemaphoreTake(reader->mutex, portMAX_DELAY);
    bool scanning = reader->is_scanning;
    xSemaphoreGive(reader->mutex);
    
    return scanning;
}