#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "interfaces/barcode.h"
#include "interfaces/proximity_sensor.h"
#include "interfaces/mfrc522.h"
#include "interfaces/ble_barcode_nimble.h"
#include "interfaces/loadcells.h"

// I2C: Proximity Sensor, IMU, 
#define SCL_PIN GPIO_NUM_9
#define SDA_PIN GPIO_NUM_8

#define PROXIMITY_INT_PIN GPIO_NUM_15
#define PROXIMITY_THRESHOLD 30

// SPI: Payment
#define MOSI_PIN GPIO_NUM_11
#define SCK_PIN  GPIO_NUM_12
#define MISO_PIN GPIO_NUM_13
#define PAYMENT_CS_PIN   GPIO_NUM_10
#define PAYMENT_RST_PIN  GPIO_NUM_14

#define LOAD_DATA_PIN GPIO_NUM_40
#define LOAD_CLK_PIN GPIO_NUM_41

// UART: Barcode scanner, Item RFID, Customer RFID
#define BARCODE_TX_PIN GPIO_NUM_38
#define BARCODE_RX_PIN GPIO_NUM_39

//Other
#define BUTTON_PIN GPIO_NUM_37
#define TAG "MAIN"

static barcode_t scanner;
static ProximitySensor* proximity_sensor = NULL;
static mfrc522_t paymenter;
static LoadCell* load_cell = NULL;

static QueueHandle_t button_evt_queue = NULL;
static QueueHandle_t proximity_evt_queue = NULL;

static bool continuous_mode = false;

static void IRAM_ATTR button_isr(void *arg)
{
    uint32_t evt = 1;
    xQueueSendFromISR(button_evt_queue, &evt, NULL);
    ESP_EARLY_LOGI(TAG, "Button interrupt triggered");
}

static void IRAM_ATTR proximity_isr(void *arg)
{
    uint32_t evt = 1;
    xQueueSendFromISR(proximity_evt_queue, &evt, NULL);
    ESP_EARLY_LOGI(TAG, "Proximity interrupt triggered");
}

void app_main(void)
{
    // Initialize BLE first (NimBLE version)
    esp_err_t ble_ret = ble_init("ESP32_Barcode_Scanner");
    if (ble_ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE initialization failed, but continuing...");
    } else {
        ESP_LOGI(TAG, "BLE barcode service initialized (NimBLE)");
    }

    barcode_init(&scanner, UART_NUM_1, BARCODE_TX_PIN, BARCODE_RX_PIN, true);
    barcode_set_manual_mode(&scanner);
    ESP_LOGI(TAG, "Barcode scanner ready in manual mode");

    proximity_sensor = proximity_sensor_create(PROXIMITY_INT_PIN, PROXIMITY_THRESHOLD, false);
    if (proximity_sensor == NULL || !proximity_sensor_begin(proximity_sensor, I2C_NUM_0, SDA_PIN, SCL_PIN, 100000)) {
        ESP_LOGE(TAG, "Failed to initialize proximity sensor");
        return;
    }
    proximity_sensor_enable_interrupt(proximity_sensor);
    ESP_LOGI(TAG, "Proximity sensor ready with threshold %d", PROXIMITY_THRESHOLD);

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE, 
    };
    gpio_config(&io_conf);

    gpio_config_t prox_io_conf = {
        .pin_bit_mask = 1ULL << PROXIMITY_INT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE, 
    };
    gpio_config(&prox_io_conf);

    button_evt_queue = xQueueCreate(4, sizeof(uint32_t));
    proximity_evt_queue = xQueueCreate(4, sizeof(uint32_t));
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr, NULL);
    gpio_isr_handler_add(PROXIMITY_INT_PIN, proximity_isr, NULL);

    ESP_LOGI(TAG, "Initializing MFRC522 payment card reader...");
    esp_err_t mfrc_result = mfrc522_init(&paymenter, SPI2_HOST, MISO_PIN, MOSI_PIN, SCK_PIN, PAYMENT_CS_PIN, PAYMENT_RST_PIN);
    if (mfrc_result != ESP_OK) {
        ESP_LOGE(TAG, "MFRC522 initialization failed! Payment card reader will not work.");
    } else {
        ESP_LOGI(TAG, "MFRC522 initialized successfully!");
    }

    load_cell = load_cell_create(LOAD_CLK_PIN, LOAD_DATA_PIN, 25, false);
    load_cell_begin(load_cell);


    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Ready: press button on GPIO %d to trigger scan or approach proximity sensor.", BUTTON_PIN);
    ESP_LOGI(TAG, "BLE Status: %s", ble_is_connected() ? "Connected" : "Waiting for connection...");

    // --- Main task loop ---
    uint8_t uid[10], uid_len = 0;
    char buf[128];
    float produce_weight = 0; 
    char *produce_weight_str = buf;

    while (1)
    {       
        uint32_t evt;
        
        if (xQueueReceive(button_evt_queue, &evt, pdMS_TO_TICKS(10)))
        {
            produce_weight = load_cell_display_pounds(load_cell);
            ESP_LOGI(TAG, "Button interrupt → produce weight: %.4f lbs", produce_weight);
            snprintf(produce_weight_str, sizeof(buf), "%.4f", produce_weight);
            ble_send_produce_weight(produce_weight_str);
            
            if (!continuous_mode) {
                ESP_LOGI(TAG, "Button press detected → triggering manual scan");
                barcode_trigger_scan(&scanner);
            }
        }

        if (xQueueReceive(proximity_evt_queue, &evt, pdMS_TO_TICKS(10)))
        {
            uint8_t proximity_value = proximity_sensor_read(proximity_sensor);
            ESP_LOGI(TAG, "Proximity interrupt → value: %d", proximity_value);

            if (proximity_value > PROXIMITY_THRESHOLD && !continuous_mode) {
                ESP_LOGI(TAG, "Proximity threshold exceeded → switching to continuous scan mode");
                barcode_set_continuous_mode(&scanner);
                continuous_mode = true;
            }

            proximity_sensor_clear_interrupt(proximity_sensor);
        }

        if (barcode_read_line(&scanner, buf, sizeof(buf)))
        {
            ESP_LOGI(TAG, "Scanned: %s", buf);
            
            // Send barcode data over BLE
            if (ble_is_connected()) {
                esp_err_t send_ret = ble_send_barcode(buf);
                if (send_ret == ESP_OK) {
                    ESP_LOGI(TAG, "✓ Barcode sent via BLE");
                } else {
                    ESP_LOGW(TAG, "✗ Failed to send barcode via BLE");
                }
            } else {
                ESP_LOGW(TAG, "⚠ BLE not connected - barcode not sent");
            }
            
            if (continuous_mode) {
                ESP_LOGI(TAG, "Barcode read → switching back to manual scan mode");
                barcode_set_manual_mode(&scanner);
                continuous_mode = false;
            }
        }

        if (mfrc522_read_uid(&paymenter, uid, &uid_len) == ESP_OK && uid_len > 0) {
            printf("[MAIN] Payment card detected: ");
            for (int i = 0; i < uid_len; i++) {
                printf("%02X ", uid[i]);
            }
            printf("\n");

            uint8_t authorized_uid[] = {0x1A, 0x83, 0x26, 0x03, 0xBC};
            bool match = (uid_len == 5);
            for (int i = 0; i < 5 && match; i++) {
                if (uid[i] != authorized_uid[i]) match = false;
            }

            if (match) {
                printf("💳 Payment Successful!\n");
                ble_send_payment_status("1");
            } else {
                printf("🚫 Payment Declined. Try another card.\n");
                ble_send_payment_status("0");
            }

            vTaskDelay(pdMS_TO_TICKS(500)); 
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}