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

// BLE Cart Tracking Transfer Configuration
#define BLE_CT_CHUNK_SIZE           100  // Reduced from 240 for safety
#define BLE_CT_INITIAL_DELAY_MS     10   // First few chunks: fast start
#define BLE_CT_INITIAL_CHUNK_COUNT  5    // Number of chunks at initial delay
#define BLE_CT_BULK_DELAY_MS        300   // Main data transfer delay
#define BLE_CT_FINAL_DELAY_MS       150  // Last chunks before FILE_END
#define BLE_CT_FILE_END_RETRY_MAX   3    // Maximum FILE_END retry attempts
#define BLE_CT_FILE_END_RETRY_DELAY 200  // Delay between FILE_END retries (ms)

#define UART_PORT   CART_TRACKING_UART_PORT
#define TX_PIN      CART_TRACKING_TX_PIN
#define RX_PIN      CART_TRACKING_RX_PIN
#define BUF_SIZE    2048

static const char *TAG = "CartTracking";

// Global flag to track if BLE transfer is active
static bool ble_transfer_in_progress = false;

// Valid RFID tags whitelist
static const char *VALID_TAGS[] = {
    "E2801170000002076A50957C",
    "E2801170000002076A508C7E",
    "E2801170000002076A509570",
    "E2801170000002076A50957A",
    "E2801170000002076A508C7D",
    "E2801170000002076A50957B",
    "E2801170000002076A50957D",
    "E2801170000002076A508D78",
    "E2801170000002076A509C7C",
    "E2801170000002076A509478",
    "E2801170000002076A509477",
    "E2801170000002076A509476"
};
#define VALID_TAG_COUNT (sizeof(VALID_TAGS) / sizeof(VALID_TAGS[0]))

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
uint8_t clearCmd[] = {0x43, 0x4D, 0x08, 0x02, 0x02, 0x00, 0x00, 0x00, 0x03};

struct TagRecord {
    char tag[64];
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

// Forward declarations
void saveBurstToFile(void);
bool isValidTag(const char *tag);

// -------------------------- Print Helper --------------------------
void printBurst(void) {
    // uart_write_bytes(UART_PORT, (const char *)clearCmd, sizeof(clearCmd));
    // vTaskDelay(pdMS_TO_TICKS(10));
    printf("\n===== TAG BURST =====\n");
    for (int i = 0; i < tagCount; i++) {
        printf("Tag: %s | Time: %lu ms\n",
        tags[i].tag, tags[i].timestamp);
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

// Add this definition if not present, or use raw 6
// Ensure this is defined at the top of cart_tracking.c
#ifndef BLE_HS_ENOMEM
#define BLE_HS_ENOMEM 6
#endif

void endSession(bool sendBLE) {
    FILE *f = fopen("/spiffs/session.log", "rb");
    if (f) {
        if (sendBLE) {
            if (ble_is_connected()) {
                ble_transfer_in_progress = true;

                // Get file size
                fseek(f, 0, SEEK_END);
                long file_size = ftell(f);
                fseek(f, 0, SEEK_SET);

                ESP_LOGI(TAG, "Sending cart tracking log via BLE (Size: %ld bytes)", file_size);

                // --- 1. Send Header with Retry ---
                // We retry specifically for ENOMEM (rc=6)
                int retry_header = 0;
                while (ble_send_cart_tracking("FILE_START") == BLE_HS_ENOMEM && retry_header < 5) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    retry_header++;
                }
                vTaskDelay(pdMS_TO_TICKS(50));

                // --- 2. Send Data Chunks ---
                uint8_t chunk[BLE_CT_CHUNK_SIZE + 1];
                size_t bytes_read;
                size_t total_sent = 0;
                int chunk_count = 0;
                bool abort_transfer = false;

                while ((bytes_read = fread(chunk, 1, BLE_CT_CHUNK_SIZE, f)) > 0) {
                    chunk[bytes_read] = '\0'; // Null-terminate

                    // --- RETRY LOOP ---
                    int retry_chunk = 0;
                    esp_err_t ret;

                    do {
                        ret = ble_send_cart_tracking((const char *)chunk);

                        if (ret == BLE_HS_ENOMEM) {
                            // Buffer is full. This is normal. Just wait for it to drain.
                            if (retry_chunk % 5 == 0) { // Don't spam logs
                                ESP_LOGW(TAG, "BLE Congestion at chunk %d (Sent %d%%). Pausing...",
                                         chunk_count + 1, (int)((total_sent * 100) / file_size));
                            }
                            vTaskDelay(pdMS_TO_TICKS(200)); // 200ms pause to let radio catch up
                            retry_chunk++;
                        } else if (ret != ESP_OK) {
                            ESP_LOGE(TAG, "Fatal BLE Error: %d", ret);
                            abort_transfer = true;
                            break;
                        }
                    } while (ret != ESP_OK && retry_chunk < 20); // Retry for up to ~4 seconds

                    if (abort_transfer || ret != ESP_OK) {
                        ESP_LOGE(TAG, "Aborting transfer: Could not send chunk %d", chunk_count + 1);
                        break;
                    }
                    // ------------------

                    chunk_count++;
                    total_sent += bytes_read;

                    if (chunk_count % 5 == 0) {
                        int percent = (total_sent * 100) / file_size;
                        ESP_LOGI(TAG, "Sent %d%%", percent);
                    }

                    // CONSTANT DELAY: Do not speed up at the end.
                    // If anything, we should slow down if we hit congestion.
                    vTaskDelay(pdMS_TO_TICKS(BLE_CT_BULK_DELAY_MS));

                    // Wait 1 second after every 25 chunks
                    if (chunk_count % 25 == 0) {
                        ESP_LOGI(TAG, "25 chunks sent, waiting 1 second...");
                        vTaskDelay(pdMS_TO_TICKS(1000));
                    }
                }

                // --- 3. Send Footer with High Reliability ---
                if (!abort_transfer) {
                    int attempts = 0;
                    esp_err_t ret = ESP_FAIL;
                    while (attempts < 10 && ret != ESP_OK) {
                        ret = ble_send_cart_tracking("FILE_END");
                        
                        if (ret == BLE_HS_ENOMEM) {
                            ESP_LOGW(TAG, "BLE Full sending FILE_END. Waiting...");
                            vTaskDelay(pdMS_TO_TICKS(500)); // Long wait for final packet
                        } else if (ret != ESP_OK) {
                            ESP_LOGW(TAG, "Retrying FILE_END...");
                            vTaskDelay(pdMS_TO_TICKS(200));
                        }
                        attempts++;
                    }
                    
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "✓ Transfer Complete");
                    } else {
                        ESP_LOGE(TAG, "✗ Failed to send FILE_END");
                    }
                }

                ble_transfer_in_progress = false;
            } else {
                ESP_LOGW(TAG, "BLE not connected - log skipped");
            }
            fclose(f);
        }
    }
    remove("/spiffs/session.log");
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
            "  {\"tag\":\"%s\", \"time\":%lu}%s\n",
            tags[i].tag, tags[i].timestamp,
            (i == tagCount - 1) ? "" : ",");
    }
    fprintf(f, "]}\n");
    fclose(f);
}

// -------------------------- Tag Validation --------------------------
bool isValidTag(const char *tag) {
    for (int i = 0; i < VALID_TAG_COUNT; i++) {
        if (strcmp(tag, VALID_TAGS[i]) == 0) {
            return true;
        }
    }
    return false;
}

// -------------------------- Core Function --------------------------

void BurstRead_CartTracking(void) {
    //Reset state
    tagCount = 0;
    collecting = false;
    index_ = 0;
    burstDone = false;

    ESP_LOGI(TAG, "Starting burst read - sending UART commands");
    // uart_write_bytes(UART_PORT, (const char *)stopCmd, sizeof(stopCmd));
    // vTaskDelay(pdMS_TO_TICKS(10));
    uart_write_bytes(UART_PORT, (const char *)startCmd, sizeof(startCmd));
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t b = 0;

    unsigned long startTime = millis();
    const unsigned long TIMEOUT_MS = 1000; // 3 second timeout for entire burst read
    bool burst_completed = false; // Flag to exit after burst completion

    while (1) {
        int read_bytes = read_from_UART_LINE(&b);
        if (read_bytes <= 0) {
            if (tagCount > 0 && (millis() - lastFrameTime) > (BURST_GAP)) {
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

                if(currentTime - lastFrameTime > BURST_GAP) {
                    printBurst();
                    vTaskDelay(pdMS_TO_TICKS(10));
                    burst_completed = true; // Mark burst as completed
                    ESP_LOGI(TAG, "Burst done");
                    return; // Exit immediately after burst completion
                }

                // Validate tag before adding
                if (!isValidTag(tagHex)) {
                    ESP_LOGW(TAG, "Invalid tag detected (not in whitelist): %s", tagHex);
                    collecting = false;
                    index_ = 0;
                    continue;
                }

                if (tagCount < 10) {
                    strcpy(tags[tagCount].tag, tagHex);
                    tags[tagCount].timestamp = currentTime;
                    ESP_LOGI(TAG, "✓ Valid RFID Tag #%d: %s",
                             tagCount + 1, tagHex);
                    tagCount++;
                }

                collecting = false;
                index_ = 0;
                // lastFrameTime = currentTime;
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

// -------------------------- Transfer Status Check --------------------------
bool is_cart_tracking_transfer_active(void) {
    return ble_transfer_in_progress;
}