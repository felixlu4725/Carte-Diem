#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

#include "interfaces/barcode.h"
#include "interfaces/proximity_sensor.h"
#include "interfaces/mfrc522.h"
#include "interfaces/ble_barcode_nimble.h"
#include "interfaces/loadcells.h"
#include "interfaces/item_rfid.h"
#include "interfaces/imu.h"
#include "interfaces/cart_tracking.h"
#include "driver/i2c_master.h"

// ===== Pin Definitions =====
#include "cartediem_defs.h"

#define TAG "MAIN"

static barcode_t barcanner;
static ProximitySensor* proximity_sensor = NULL;
static mfrc522_t paymenter;
static ICM20948_t imu_sensor;
static item_rfid_reader_t* item_reader = NULL;

static LoadCell* produce_load_cell = NULL;
static LoadCell* cart_load_cell = NULL;

static i2c_master_bus_handle_t i2c_bus_handle = NULL;



static QueueHandle_t button_evt_queue = NULL;
static QueueHandle_t proximity_evt_queue = NULL;
static QueueHandle_t imu_idle_evt_queue = NULL;
static QueueHandle_t imu_motion_after_idle_queue = NULL;

static TaskHandle_t weight_monitor_task_handle = NULL;

static bool continuous_mode = false;
static bool payment_mode = false;
static bool cart_tracking_active = false;

// ===== Weight Monitoring Variables =====
static float last_cart_weight = 0.0f;
static float weight_change_threshold = 0.5f;
static void weight_monitor_task(void *arg); // Forward declaration

// ===== Cart Tracking Variables =====
static uint32_t last_cart_tracking_time_ms = 0;
#define CART_TRACKING_INTERVAL_MS 10000  // 10 seconds

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

void on_item_scan_complete(const item_rfid_tag_t *tags, int count) {
    ESP_LOGI(TAG, "Found %d items in cart", count);

    // Get cart weight
    // float cart_weight = load_cell_display_pounds(cart_load_cell);
    float cart_weight = load_cell_display_pounds(produce_load_cell);

    // Build verification string: "weight,num_tags,tag1,tag2,tag3,..."
    char verification_msg[512] = {0};
    int offset = snprintf(verification_msg, sizeof(verification_msg), "%.4f,%d", cart_weight, count);

    // Add each tag to the message
    for (int i = 0; i < count && offset < (int)sizeof(verification_msg) - 1; i++) {
        offset += snprintf(verification_msg + offset,
                          sizeof(verification_msg) - offset,
                          ",%s",
                          tags[i].tag);
    }

    // Send via BLE
    esp_err_t send_ret = ble_send_item_verification(verification_msg);
    if (send_ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ Cart verification sent via BLE: %s", verification_msg);
    } else {
        ESP_LOGW(TAG, "✗ Failed to send cart verification via BLE");
    }
}

// ===== BLE Receive Handler =====

static void handle_ble_command(const char *data, uint16_t len)
{
    ESP_LOGI(TAG, "BLE command received: %s (len=%d)", data, len);

    if (len == 0) {
        return;
    }

    switch (data[0]) {
        case 'T':  // "TARE_" commands
            if(strcmp("TARE_PRODUCE_WEIGHT", data) == 0 || strcmp("TARE_PROD_WEIGHT", data) == 0 || strcmp("T_PROD", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Taring produce load cell");
                load_cell_tare(produce_load_cell);
                break;
            }
            else if(strcmp("TARE_CART_WEIGHT", data) == 0 || strcmp("T_CART", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Taring cart load cell");
                load_cell_tare(cart_load_cell);
                break;
            }
            break;
        
        case 'M': // "MEASURE_" commands
            if(strcmp("MEASURE_PRODUCE_WEIGHT", data) == 0 || strcmp("MEASURE_PROD_WEIGHT", data) == 0 || strcmp("M_PROD", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Measuring produce weight");
                float weight = load_cell_display_ounces(produce_load_cell);
                char weight_str[32];
                snprintf(weight_str, sizeof(weight_str), "%.4f", weight);
                ble_send_produce_weight(weight_str);
                break;
            }
            else if(strcmp("MEASURE_CART_WEIGHT", data) == 0 || strcmp("M_CART", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Measuring cart weight");
                float weight = load_cell_display_pounds(cart_load_cell);
                char weight_str[32];
                snprintf(weight_str, sizeof(weight_str), "[CART_LOAD] %.4f", weight);
                ble_send_misc_data(weight_str);
                break;
            }
            break;

        case 'P':  // Payment module or Proximity sensor commands
            if(strcmp("PAY_START", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Checking payment status - enabling payment module");
                payment_mode = true;
                ESP_LOGI(TAG, "Payment task: Waiting for card...");
                break;
            }
            else if(strcmp("PAY_READ", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Reading payment card UID");
                uint8_t tmp_uid[10], tmp_uid_len = 0;
                if (mfrc522_read_uid(&paymenter, tmp_uid, &tmp_uid_len) == ESP_OK && tmp_uid_len > 0) {
                    char uid_str[32] = {0};
                    char *ptr = uid_str;
                    for (int i = 0; i < tmp_uid_len; i++) {
                        ptr += sprintf(ptr, "%02X", tmp_uid[i]);
                    }
                    char tmp_uid_msg[64];
                    snprintf(tmp_uid_msg, sizeof(tmp_uid_msg), "[PAY] UID: %s", uid_str);
                    ble_send_misc_data(tmp_uid_msg);
                } else {
                    ESP_LOGI(TAG, "No payment card detected");
                    ble_send_misc_data("[PAY] NO_CARD");
                }
                break;
            }
            else if(strcmp("PROX_READ", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Reading proximity sensor value");
                uint8_t proximity_value = proximity_sensor_read(proximity_sensor);
                char proximity_str[32];
                snprintf(proximity_str, sizeof(proximity_str), "[PROX] %d", proximity_value);
                ble_send_misc_data(proximity_str);
                break;
            }
            break;

        case 'I': // "IMU_" commands or "IV_" item verification commands
            if(strcmp("IMU_CHECK_ACTIVITY", data) == 0 || strcmp("IMU_STATUS", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Checking IMU activity");
                if(icm20948_is_moving(&imu_sensor)) {
                    ESP_LOGI(TAG, "IMU reports: Cart is moving");
                    ble_send_misc_data("[IMU] MOVING");
                } else {
                    if(imu_sensor.idle_counter_ms >= IMU_IDLE_TIME_MINUTES * 60 * 1000){
                        ESP_LOGI(TAG, "IMU reports: Cart has been idle for 5 minutes");
                        ble_send_misc_data("[IMU] IDLE");
                        return;
                    }
                    else{
                        ESP_LOGI(TAG, "IMU reports: Cart has been stopped");
                        ble_send_misc_data("[IMU] STOPPED");
                    }
                }
            }
            else if(strcmp("IMU_IDLE_TIME", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Getting IMU idle time");
                uint32_t idle_time_ms = imu_sensor.idle_counter_ms;
                char idle_time_str[64];
                snprintf(idle_time_str, sizeof(idle_time_str), "[IMU] IDLE_TIME: %lu", idle_time_ms);
                ble_send_misc_data(idle_time_str);
            }
            else if(strcmp("IMU_ACCEL", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Getting IMU acceleration");
                icm20948_read_accel(&imu_sensor);
                char accel_str[64];
                snprintf(accel_str, sizeof(accel_str), "[IMU] ACCEL: X=%.2f, Y=%.2f, Z=%.2f",
                         imu_sensor.accel.x, imu_sensor.accel.y, imu_sensor.accel.z);
                ble_send_misc_data(accel_str);
            }
            else if(strcmp("IMU_HEADING", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Getting IMU heading");
                float heading = icm20948_compute_heading(&imu_sensor);
                char heading_str[32];
                snprintf(heading_str, sizeof(heading_str), "[IMU] HEADING: %.2f", heading);
                ble_send_misc_data(heading_str);
            }
            else if(strcmp("IV_TRIG", data) == 0 || strcmp("IV_SCAN", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Force triggering item scan");
                // item_rfid_scan(item_reader); // callback at on_item_scan_complete()
                break;
            }
            break;
        
        case 'C': // Cart Tracking - txt file commands
            if(strcmp("CT_START", data) == 0) {
                ESP_LOGI(TAG, "Starting cart tracking data logging");

                // Start cart tracking session
                // startSession();

                // Tare produce load cell
                load_cell_tare(produce_load_cell);
                ESP_LOGI(TAG, "Load cell tared for tracking");
                last_cart_weight = load_cell_display_pounds(produce_load_cell);

                // Perform initial item scan
                // item_rfid_scan(item_reader);

                // Create weight monitoring task (1 second interval)
                // xTaskCreate(weight_monitor_task, "weight_monitor", 8192, NULL, 4, &weight_monitor_task_handle);
                ESP_LOGI(TAG, "Weight monitoring task created (threshold: %.4f lbs)", weight_change_threshold);

                // Enable cart tracking
                // cart_tracking_active = true;
            }
            else if(strcmp("CT_STOP", data) == 0) {
                // ESP_LOGI(TAG, "Exporting cart tracking data log via BLE");

                // Disable cart tracking
                // cart_tracking_active = false;

                // Stop weight monitoring
                if (weight_monitor_task_handle != NULL) {
                    vTaskDelete(weight_monitor_task_handle);
                    weight_monitor_task_handle = NULL;
                    ESP_LOGI(TAG, "Weight monitoring task stopped");
                }

                // End session and send log via BLE
                // endSession(true);
            }
            else if(strcmp("CT_CLEAR", data) == 0) {
                // ESP_LOGI(TAG, "Clearing cart tracking data log");

                // Disable cart tracking
                // cart_tracking_active = false;

                // Stop weight monitoring
                if (weight_monitor_task_handle != NULL) {
                    vTaskDelete(weight_monitor_task_handle);
                    weight_monitor_task_handle = NULL;
                    ESP_LOGI(TAG, "Weight monitoring task stopped");
                }

                // End session and remove file without sending
                // endSession(false);
            }
            break;

        default:
            ESP_LOGW(TAG, "BLE Command: Unknown command '%c' (full: %s)", data[0], data);
            ble_send_misc_data("[ERROR] UNKNOWN_CMD");
            break;
    }
}

// ===== IMU Idle Event Handler (called from main loop, not from timer context) =====

static void handle_imu_idle_event(void)
{
    ESP_LOGI(TAG, "⏱ IMU: Cart idle for 5 minutes - no motion detected");
    ble_send_misc_data("[IMU] IDLE");
}

// ===== IMU Motion After Idle Event Handler =====

static void handle_imu_motion_after_idle_event(void)
{
    ESP_LOGI(TAG, "🚀 IMU: Motion detected after 5+ minute idle");
    ble_send_misc_data("[IMU] Moving");
}

// ===== Individual Setup Functions =====

static void i2c_setup(void)
{
    ESP_LOGI(TAG, "Initializing I2C bus...");
    i2c_master_bus_config_t i2c_bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
        },
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_handle));
    ESP_LOGI(TAG, "I2C master bus initialized");
}

static void ble_setup(void)
{
    ESP_LOGI(TAG, "Initializing BLE...");
    esp_err_t ble_ret = ble_init("Carte_Diem");
    if (ble_ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE initialization failed, but continuing...");
    } else {
        ESP_LOGI(TAG, "BLE barcode service initialized (NimBLE)");
        ble_register_rx_callback(handle_ble_command);
    }
}

static void barcode_setup(void)
{
    ESP_LOGI(TAG, "Initializing barcode scanner...");
    barcode_init(&barcanner, BARCODE_UART_PORT, BARCODE_TX_PIN, BARCODE_RX_PIN, true);
    ESP_LOGI(TAG, "Barcode scanner ready in manual mode");
}

static void button_setup(void)
{
    ESP_LOGI(TAG, "Initializing button...");
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);

    button_evt_queue = xQueueCreate(4, sizeof(uint32_t));
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr, NULL);
    ESP_LOGI(TAG, "Button ready on GPIO %d", BUTTON_PIN);
}

static void proximity_setup(void)
{
    ESP_LOGI(TAG, "Initializing proximity sensor...");
    proximity_sensor = proximity_sensor_create(PROXIMITY_INT_PIN, PROXIMITY_THRESHOLD, false);
    if (proximity_sensor == NULL || !proximity_sensor_begin(proximity_sensor, i2c_bus_handle)) {
        ESP_LOGE(TAG, "Failed to initialize proximity sensor");
        return;
    }
    proximity_sensor_enable_interrupt(proximity_sensor);
    ESP_LOGI(TAG, "Proximity sensor ready with threshold %d", PROXIMITY_THRESHOLD);
}

static void imu_setup(void){
    ESP_LOGI(TAG, "Initializing IMU...");
    icm20948_init(&imu_sensor, i2c_bus_handle);
    ESP_LOGI(TAG, "IMU initialized successfully");

    imu_idle_evt_queue = xQueueCreate(4, sizeof(uint32_t));
    if (imu_idle_evt_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create IMU idle event queue");
        return;
    }

    imu_motion_after_idle_queue = xQueueCreate(4, sizeof(uint32_t));
    if (imu_motion_after_idle_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create IMU motion after idle event queue");
        return;
    }

    // Connect queues to IMU sensor so it can send events
    imu_sensor.idle_event_queue = imu_idle_evt_queue;
    imu_sensor.motion_after_idle_queue = imu_motion_after_idle_queue;

    // Try accel read (but don't exit)
    if(icm20948_read_accel(&imu_sensor) != ESP_OK){
        ESP_LOGW(TAG, "IMU WARNING: accel read failed. System will still run but IMU will be inaccurate");
    } else {
        ESP_LOGI(TAG, "IMU accelerometer verified working");
    }

    // Create dedicated IMU monitoring task (1 second interval)
    xTaskCreate(icm20948_monitor_task, "imu_monitor", 2048, &imu_sensor, 5, NULL);
    ESP_LOGI(TAG, "IMU monitoring task created (5-minute idle timeout)");
}


static void proximity_interrupt_setup(void)
{
    ESP_LOGI(TAG, "Initializing proximity interrupt...");
    gpio_config_t prox_io_conf = {
        .pin_bit_mask = 1ULL << PROXIMITY_INT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&prox_io_conf);

    proximity_evt_queue = xQueueCreate(4, sizeof(uint32_t));
    gpio_isr_handler_add(PROXIMITY_INT_PIN, proximity_isr, NULL);
    ESP_LOGI(TAG, "Proximity interrupt ready on GPIO %d", PROXIMITY_INT_PIN);
}

static void payment_setup(void)
{
    ESP_LOGI(TAG, "Initializing MFRC522 payment card reader...");
    esp_err_t mfrc_result = mfrc522_init(&paymenter, SPI2_HOST, MISO_PIN, MOSI_PIN, SCK_PIN, PAYMENT_CS_PIN, PAYMENT_RST_PIN);
    if (mfrc_result != ESP_OK) {
        ESP_LOGE(TAG, "MFRC522 initialization failed! Payment card reader will not work.");
    } else {
        ESP_LOGI(TAG, "MFRC522 initialized successfully!");
    }
}

static void produce_loadcell_setup(void)
{
    ESP_LOGI(TAG, "Initializing load cell...");
    produce_load_cell = load_cell_create(TOP_LOAD_CLK_PIN, TOP_LOAD_DATA_PIN, 25, false);
    load_cell_begin(produce_load_cell);
    load_cell_tare(produce_load_cell);
    ESP_LOGI(TAG, "Load cell initialized and tared.");
}

static void cart_loadcell_setup(void)
{
    ESP_LOGI(TAG, "Initializing load cell...");
    cart_load_cell = load_cell_create(BOTTOM_LOAD_CLK_PIN, BOTTOM_LOAD_DATA_PIN, 25, true);
    load_cell_begin(cart_load_cell);
    load_cell_tare(cart_load_cell);
    ESP_LOGI(TAG, "Load cell initialized and tared.");
}

static void item_rfid_setup(void)
{
    ESP_LOGI(TAG, "Initializing item RFID reader...");
    item_reader = item_rfid_init(
        ITEM_RFID_UART_PORT,
        ITEM_RFID_TX_PIN,
        ITEM_RFID_RX_PIN,
        on_item_scan_complete
    );
    ESP_LOGI(TAG, "Item RFID reader initialized");
}

static void cart_tracking_setup(void) {
    ESP_LOGI(TAG, "Setting up cart tracking...");

    SetUpCartTracking();
    InitFileSystem();

    ESP_LOGI(TAG, "Cart tracking setup complete.");
}

// ===== Weight Monitoring Task =====
static void weight_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "Weight monitoring task started (1 second interval)");

    while (1) {
        // float current_weight = load_cell_display_pounds(cart_load_cell);
        float current_weight = load_cell_display_pounds(produce_load_cell);

        float weight_delta = fabs(current_weight - last_cart_weight);

        ESP_LOGD(TAG, "[Weight Monitor] Current: %.4f lbs, Last: %.4f lbs, Delta: %.4f lbs, Threshold: %.4f lbs",
                 current_weight, last_cart_weight, weight_delta, weight_change_threshold);

        if (weight_delta > weight_change_threshold) {
            ESP_LOGI(TAG, "Weight change detected! Diff.: %.4f lbs (threshold: %.4f lbs)",
                 weight_delta, weight_change_threshold);
            ESP_LOGI(TAG, "Triggering automatic item RFID scan...");

            if (!item_rfid_is_scanning(item_reader)) {
                esp_err_t scan_ret = item_rfid_scan(item_reader);
                if (scan_ret == ESP_OK) {
                    ESP_LOGI(TAG, "✓ RFID scan triggered successfully");
                } else {
                    ESP_LOGW(TAG, "✗ Failed to trigger RFID scan (error: %d)", scan_ret);
                }
            } else {
                ESP_LOGD(TAG, "RFID scan already in progress, skipping trigger");
            }
        }

        last_cart_weight = current_weight;

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void setup(void)
{
    ESP_LOGI(TAG, "Starting system initialization...");

    // Initialize I2C bus first (used by proximity and IMU)
    i2c_setup();

    ble_setup();
    barcode_setup();
    button_setup();

    proximity_setup();
    proximity_interrupt_setup();
    imu_setup();
    payment_setup();
    produce_loadcell_setup();
    // cart_loadcell_setup();
    item_rfid_setup();

    // cart_tracking_setup();

    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "System initialization complete");
    ESP_LOGI(TAG, "Ready: press button on GPIO %d to trigger scan or approach proximity sensor.", BUTTON_PIN);
    ESP_LOGI(TAG, "BLE Status: %s", ble_is_connected() ? "Connected" : "Waiting for connection...");
}

void app_main(void)
{
    setup();

    // --- Main task loop ---
    uint8_t uid[10], uid_len = 0;
    char buf[128];
    float produce_weight = 0;
    char *produce_weight_str = buf;
    uint8_t authorized_uid[] = {0x1A, 0x83, 0x26, 0x03, 0xBC};


    while (1)
    {
        uint32_t evt;

        // button press
        if (xQueueReceive(button_evt_queue, &evt, pdMS_TO_TICKS(10)))
        {
            if (!continuous_mode) {
                ESP_LOGI(TAG, "Button press detected → triggering manual scan");
                barcode_trigger_scan(&barcanner);
            }
        }

        // proximity interrupt
        if (xQueueReceive(proximity_evt_queue, &evt, pdMS_TO_TICKS(10)))
        {
            uint8_t proximity_value = proximity_sensor_read(proximity_sensor);
            ESP_LOGI(TAG, "Proximity interrupt → value: %d", proximity_value);

            if (proximity_value > PROXIMITY_THRESHOLD && !continuous_mode) {
                ESP_LOGI(TAG, "Proximity threshold exceeded → switching to continuous scan mode");
                barcode_set_continuous_mode(&barcanner);
                continuous_mode = true;
            }

            proximity_sensor_clear_interrupt(proximity_sensor);
        }

        // IMU idle for 5 minutes
        if (xQueueReceive(imu_idle_evt_queue, &evt, pdMS_TO_TICKS(10)))
        {
            handle_imu_idle_event();
        }

        // IMU motion detected after 5+ minute idle
        if (xQueueReceive(imu_motion_after_idle_queue, &evt, pdMS_TO_TICKS(10)))
        {
            handle_imu_motion_after_idle_event();
        }

        // barcode reading
        if (barcode_read_line(&barcanner, buf, sizeof(buf)))
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
                barcode_set_manual_mode(&barcanner);
                continuous_mode = false;
            }
        }

        // Cart tracking RFID burst read (every 10 seconds, only when IMU is moving)
        // if (cart_tracking_active) { // 
        //     uint32_t current_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        //     uint32_t time_since_last_tracking = current_time_ms - last_cart_tracking_time_ms;

        //     if (time_since_last_tracking >= CART_TRACKING_INTERVAL_MS) {
        //         BurstRead_CartTracking();
        //         last_cart_tracking_time_ms = current_time_ms;
        //     }
        // }

        // Payment processing
        if (payment_mode) {

            if (mfrc522_read_uid(&paymenter, uid, &uid_len) == ESP_OK && uid_len > 0) {
                printf("[MAIN] Payment card detected: ");
                for (int i = 0; i < uid_len; i++) {
                    printf("%02X ", uid[i]);
                }
                printf("\n");

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

                payment_mode = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}