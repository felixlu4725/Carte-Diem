#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"

#include "cartediem_defs.h"
#include "ble_barcode_nimble.h"
#include "cart_tracking.h"

#define UART_PORT   CART_TRACKING_UART_PORT
#define TX_PIN      CART_TRACKING_TX_PIN
#define RX_PIN      CART_TRACKING_RX_PIN
#define BUF_SIZE    2048

static const char *TAG = "CartTracking";

// -------------------------- Time Simulation --------------------------
unsigned long millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (unsigned long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// -------------------------- UART Read Helper --------------------------
int read_from_UART_LINE(uint8_t *byte_out) {
    return uart_read_bytes(UART_PORT, byte_out, 1, 20 / portTICK_PERIOD_MS);
}

// -------------------------- Commands --------------------------
uint8_t startCmd[] = {0x43, 0x4D, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00};
uint8_t stopCmd[] = {0x43, 0x4D, 0x03, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00};

struct TagRecord {
    char tag[64];
    int rssi;
    int antenna;
    unsigned long timestamp;
};

struct TagRecord tags[10];
int tagCount = 0;

uint8_t buffer[64];
int index_ = 0;
bool collecting = false;

unsigned long lastFrameTime = 0;
bool burstDone = false;
const unsigned long BURST_GAP = 1000; //ms

// Forward declaration
void saveBurstToFile(void);

// -------------------------- Print Helper --------------------------
void printBurst(void) {
    printf("\n===== TAG BURST =====\n");
    for (int i = 0; i < tagCount; i++) {
        printf("Tag: %s | RSSI: %d dBm | Antennna: %d | Time: %lu ms\n",
        tags[i].tag, tags[i].rssi, tags[i].antenna, tags[i].timestamp);
    }
    printf("Tags scanned: %d\n=====================\n\n", tagCount);
    saveBurstToFile(); //append to session log
    tagCount = 0;
}

void startSession(void){
    remove("/spiffs/session.log");  // ensure old file is gone
    FILE *f = fopen("/spiffs/session.log", "w");
    if (f) {
        fclose(f);
        ESP_LOGI("SESSION", "Session started, new log created.");
    } else {
        ESP_LOGE("SESSION", "Failed to create log file.");
    }
}

void endSession(bool sendBLE) {
    FILE *f = fopen("/spiffs/session.log", "rb");
    if (f) {
        if (sendBLE) {
            if (ble_is_connected()) {
                // Get file size
                fseek(f, 0, SEEK_END);
                long file_size = ftell(f);
                fseek(f, 0, SEEK_SET);

                ESP_LOGI(TAG, "Sending cart tracking log via BLE (protocol: FILE_START/DATA/FILE_END)");

                // Send FILE_START header
                char header[128];
                snprintf(header, sizeof(header), "FILE_START");
                esp_err_t ret = ble_send_cart_tracking(header);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "✗ Failed to send FILE_START");
                    fclose(f);
                    return;
                }
                vTaskDelay(pdMS_TO_TICKS(50));

                // Send file data in chunks with progressive delays
                uint8_t chunk[BLE_CT_CHUNK_SIZE + 1];
                size_t bytes_read;
                size_t total_sent = 0;
                int chunk_count = 0;

                while ((bytes_read = fread(chunk, 1, BLE_CT_CHUNK_SIZE, f)) > 0) {
                    chunk[bytes_read] = '\0';  // Null-terminate for BLE string send

                    ret = ble_send_cart_tracking((const char *)chunk);
                    if (ret != ESP_OK) {
                        ESP_LOGW(TAG, "✗ Failed to send data chunk %d", chunk_count + 1);
                        break;
                    }

                    chunk_count++;
                    total_sent += bytes_read;
                    int percent = (total_sent * 100) / file_size;
                    ESP_LOGI(TAG, "Sent %d%%", percent);

                    // Progressive delay to prevent mbuf pool exhaustion
                    uint32_t chunk_delay;
                    if (chunk_count <= BLE_CT_INITIAL_CHUNK_COUNT) {
                        chunk_delay = BLE_CT_INITIAL_DELAY_MS;  // Fast start
                    } else if (total_sent > (file_size * 90) / 100) {
                        chunk_delay = BLE_CT_FINAL_DELAY_MS;  // Safety margin for FILE_END
                    } else {
                        chunk_delay = BLE_CT_BULK_DELAY_MS;  // Main transfer
                    }
                    vTaskDelay(pdMS_TO_TICKS(chunk_delay));
                }

                // Send FILE_END with retry logic (critical protocol marker)
                uint8_t file_end_attempts = 0;
                ret = ESP_FAIL;
                while (file_end_attempts < BLE_CT_FILE_END_RETRY_MAX && ret != ESP_OK) {
                    file_end_attempts++;
                    ret = ble_send_cart_tracking("FILE_END");

                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "✓ Cart tracking log sent via BLE (%.1f KB)", file_size / 1024.0);
                        break;
                    }

                    if (file_end_attempts < BLE_CT_FILE_END_RETRY_MAX) {
                        ESP_LOGW(TAG, "✗ FILE_END attempt %d/%d failed, retrying in %dms...",
                                 file_end_attempts, BLE_CT_FILE_END_RETRY_MAX, BLE_CT_FILE_END_RETRY_DELAY);
                        vTaskDelay(pdMS_TO_TICKS(BLE_CT_FILE_END_RETRY_DELAY));
                    } else {
                        ESP_LOGE(TAG, "✗ Failed to send FILE_END after %d attempts", file_end_attempts);
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            } else {
                ESP_LOGW(TAG, "⚠ BLE not connected - log not sent");
            }
            fclose(f);
        } else {
            // Just print the log without sending
            // char line[128];
            // while (fgets(line, sizeof(line), f)) {
            //     printf("%s", line);
            // }
            // fclose(f);
        }
    } else {
        ESP_LOGW(TAG, "Failed to open session log file");
    }

    // Clean up the session file
    if (remove("/spiffs/session.log") == 0) {
        ESP_LOGI(TAG, "Session file deleted successfully");
    } else {
        ESP_LOGE(TAG, "Failed to delete session file");
    }
}

//------FILE stuff ------
void InitFileSystem(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS mounted successfully");
    }
}

//---save each burst to the file
void saveBurstToFile(void) {
    FILE *f = fopen("/spiffs/session.log", "a");
    if (!f) {
        ESP_LOGE(TAG, "FAILED to open session file");
        return;
    }
    fprintf(f, "{ \"burst\": [\n");
    for (int i = 0; i < tagCount; i++) {
        fprintf(f,
            "  {\"tag\":\"%s\", \"rssi\":%d, \"antenna\":%d, \"time\":%lu}%s\n",
            tags[i].tag, tags[i].rssi, tags[i].antenna, tags[i].timestamp,
            (i == tagCount - 1) ? "" : ",");
    }
    fprintf(f, "]}\n");
    fclose(f);
}

// -------------------------- Core Function --------------------------

void BurstRead_CartTracking(void) {
    //Reset state
    tagCount = 0;
    collecting = false;
    index_ = 0;
    burstDone = false;

    ESP_LOGI(TAG, "Starting burst read - sending UART commands");
    uart_write_bytes(UART_PORT, (const char *)stopCmd, sizeof(stopCmd));
    vTaskDelay(pdMS_TO_TICKS(10));
    uart_write_bytes(UART_PORT, (const char *)startCmd, sizeof(startCmd));
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t b = 0;

    unsigned long startTime = millis();
    const unsigned long TIMEOUT_MS = 3000; // 3 second timeout for entire burst read
    bool burst_completed = false; // Flag to exit after burst completion

    while (1) {
        int read_bytes = read_from_UART_LINE(&b);
        if (read_bytes <= 0) {
            if (tagCount > 0 && (millis() - lastFrameTime) > (BURST_GAP * 2)) {
                printBurst();
                vTaskDelay(pdMS_TO_TICKS(10));
                burstDone = false;
                ESP_LOGI(TAG, "Burst read complete - exiting");
                return; // Exit after completing a burst
            }
            // Timeout check - exit if no data for too long
            if (tagCount == 0 && (millis() - startTime) > TIMEOUT_MS) {
                ESP_LOGW(TAG, "Burst read timeout - no tags found");
                return;
            }
            continue;
        }

        if (!collecting) {
            if (b == 0x43) {
                buffer[0] = b;
                index_ = 1;
                collecting = true;
            }
            continue;
        }

        buffer[index_++] = b;

        if (index_ == 2 && buffer[1] != 0x4D) {
            collecting = false;
            index_ = 0;
            continue;
        }

        if (index_ >= sizeof(buffer)) {
            collecting = false;
            index_ = 0;
            continue;
        }

        if (index_ >= 11) {
            uint8_t tagLen = buffer[9];
            uint8_t optCtrl = buffer[10];
            uint8_t extra = (optCtrl & 0x40) ? 1 : 0;
            int expectedLen = 8 + 3 + tagLen + extra + 1; //extra might be wrong bc with antenna its maybe 2?

            if (index_ >= expectedLen) {
                unsigned long currentTime = millis();
                if (!burstDone) {
                    burstDone = true;
                    lastFrameTime = currentTime;
                }

                char tagHex[64] = {0};
                for (int i = 0; i < tagLen && i < 32; i++) {
                    char tmp[4];
                    sprintf(tmp, "%02X", buffer[11 + i]);
                    strcat(tagHex, tmp);
                }

                int antenna = 0;
                if (optCtrl & 0x07) {
                    antenna = buffer[11 + tagLen];
                }

                int rssi = -999;
                if (optCtrl & 0x40) {
                    uint8_t rssiByte = buffer[11 + tagLen + 1];
                    rssi = (int)rssiByte;
                }

                if(currentTime - lastFrameTime > BURST_GAP) {
                    printBurst();
                    vTaskDelay(pdMS_TO_TICKS(10));
                    burst_completed = true; // Mark burst as completed
                    ESP_LOGI(TAG, "Burst done");
                    return; // Exit immediately after burst completion
                }

                if (tagCount < 10) {
                    strcpy(tags[tagCount].tag, tagHex);
                    tags[tagCount].rssi = rssi;
                    tags[tagCount].antenna = antenna;
                    tags[tagCount].timestamp = currentTime;
                    ESP_LOGI(TAG, "RFID Tag #%d: %s | RSSI: %d dBm | Antenna: %d",
                             tagCount + 1, tagHex, rssi, antenna);
                    tagCount++;
                }

                collecting = false;
                index_ = 0;
                //lastFrameTime = currentTime;
            }
        }
    }
}

// -------------------------- Setup --------------------------
void SetUpCartTracking(void) {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    //ESP_LOGI(TAG, "UART initialized (TX=%d, RX=%d)", TX_PIN, RX_PIN);
}