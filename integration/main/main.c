// 2025.12.5 - Carte Diem Main Application code for ESP32-S3 based PCB system

#define TAG "MAIN"

// ===== Defines and Includes =====
#include "cartediem_defs.h"

// ===== Instances =====
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

static TaskHandle_t item_verification_task_handle = NULL;
static TaskHandle_t imu_monitor_task_handle = NULL;
static TaskHandle_t cart_tracking_task_handle = NULL;

// ===== Variables =====
static bool mode_continuous = false;
static bool mode_payment = false;
static bool mode_cart_tracking = false;
static bool mode_outdoor = false;


static uint32_t last_button_isr_time_ms = 0;
static uint32_t last_proximity_isr_time_ms = 0;

// ===== Forward Declarations =====
static void ble_setup(void);
static void i2c_setup(void);
static void imu_setup(void);
static void proximity_setup(void);
static void proximity_interrupt_setup(void);
static void barcode_setup(void);
static void button_setup(void);
static void payment_setup(void);
static void produce_loadcell_setup(void);
static void cart_loadcell_setup(void);
static void item_rfid_setup(void);
static void cart_tracking_setup(void);

static void IRAM_ATTR button_isr(void *arg);
static void IRAM_ATTR proximity_isr(void *arg);
static void handle_ble_command(const char *data, uint16_t len);
static void handle_imu_idle_event(void);
static void handle_imu_motion_after_idle_event(void);
void on_item_scan_complete(const item_rfid_tag_t *tags, int count);
static void safe_ble_send_misc_data(const char *data);

static void item_verification_task(void *arg);
static void cart_tracking_task(void *arg);
static void icm20948_monitor_task(void *arg);

static void outdoor_setting();
static void indoor_setting();
static void debug_led();
static bool try_payment(uint8_t *uid, uint8_t uid_len);

// ===== Main Setup Function =====
static void setup(void)
{
    ESP_LOGI(TAG, "[[Carte Diem]]");
    ESP_LOGI(TAG, "Starting system initialization...");
    
    ble_setup();

    barcode_setup();
    barcode_set_manual_mode(&barcanner);
    button_setup();

    i2c_setup();
    imu_setup(); 
    #if ENABLE_PROXIMITY_SENSOR
    proximity_setup();
    proximity_interrupt_setup();
    #endif

    payment_setup();
    produce_loadcell_setup();
    cart_loadcell_setup();

    #if ENABLE_ITEM_VERIFICATION
    item_rfid_setup();
    #endif

    #if ENABLE_CART_TRACKING
    cart_tracking_setup();
    #endif

    #if ENABLE_LED_DEBUG_STARTUP
    debug_led();
    #endif

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "|==================================|");
    ESP_LOGI(TAG, "|  System initialization complete  |");
    ESP_LOGI(TAG, "|==================================|");

    #if !ENABLE_ITEM_VERIFICATION
    ESP_LOGI(TAG, "Item Verification DISABLED");
    #endif
    #if !ENABLE_CART_TRACKING
    ESP_LOGI(TAG, "Cart Tracking DISABLED");
    #endif

    ESP_LOGI(TAG, "BLE Status: %s", ble_is_connected() ? "Connected" : "Waiting for connection...");
}

void app_main(void)
{
    setup();

    // --- Main task loop ---
    uint8_t uid[10], uid_len = 0;
    char buf[128];

    while (1)
    {
        uint32_t evt;

        // button press
        if (xQueueReceive(button_evt_queue, &evt, pdMS_TO_TICKS(10)))
        {       
            ESP_LOGI(TAG, "Button press detected → triggering manual scan");
            barcode_trigger_scan(&barcanner);
        }

        // proximity interrupt
        #if ENABLE_PROXIMITY_SENSOR
        if (xQueueReceive(proximity_evt_queue, &evt, pdMS_TO_TICKS(10)))
        {
            uint8_t proximity_value = proximity_sensor_read(proximity_sensor);
            ESP_LOGI(TAG, "Proximity interrupt → value: %d", proximity_value);

            if (proximity_value > PROXIMITY_THRESHOLD && !mode_continuous) {
                ESP_LOGI(TAG, "Proximity threshold exceeded → switching to continuous scan mode");
                barcode_set_continuous_mode(&barcanner);
                mode_continuous = true;
            }

            ESP_LOGI(TAG, "Proximity interrupt → triggering manual scan");
            proximity_sensor_clear_interrupt(proximity_sensor);

            barcode_trigger_scan(&barcanner);
        }
       
        if (gpio_get_level(PROXIMITY_INT_PIN) == 0) {
            uint8_t current_prox = proximity_sensor_read(proximity_sensor);
            if (current_prox < PROXIMITY_THRESHOLD) {
                proximity_sensor_clear_interrupt(proximity_sensor);
            }
        }
        #endif


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

            #if ENABLE_PROXIMITY_SENSOR
            if (mode_continuous) {
                ESP_LOGI(TAG, "Barcode read → switching back to manual scan mode");
                barcode_set_manual_mode(&barcanner);
                mode_continuous = false;
            }
            #endif
        }

        // Payment processing
        if (mode_payment) {
            if (mfrc522_read_uid(&paymenter, uid, &uid_len) == ESP_OK && uid_len > 0) {
                try_payment(uid, uid_len);
                mode_payment = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


// Functions...

// ===== BLE Receive Handler =====

static void handle_ble_command(const char *data, uint16_t len)
{
    ESP_LOGI(TAG, "BLE command received: %s (len=%d)", data, len);

    if (len == 0) {
        return;
    }

    if (mode_outdoor) {
        if(strcmp("INDOOR_MODE_ON", data) == 0){
            ESP_LOGI(TAG, "BLE Command: Switching to INDOOR mode");
            indoor_setting();
            safe_ble_send_misc_data("[MODE] INDOOR MODE ON");
            mode_outdoor = false;
            return;
        }
        return;
    }

    // Indoor mode commands
    switch (data[0]) {        
        case 'T':  // "TARE_" commands
            if(strcmp("TARE_PRODUCE_WEIGHT", data) == 0 || strcmp("TARE_PROD_WEIGHT", data) == 0 || strcmp("T_PROD", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Taring produce load cell");
                load_cell_tare(produce_load_cell);
                ESP_LOGI(TAG, "produce taring done");
                break;
            }
            else if(strcmp("TARE_CART_WEIGHT", data) == 0 || strcmp("T_CART", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Taring cart load cell");
                load_cell_tare(cart_load_cell);
                ESP_LOGI(TAG, "cart taring done");
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
                safe_ble_send_misc_data(weight_str);
                break;
            }
            break;

        case 'P':  // Payment module or Proximity sensor commands
            if(strcmp("PAY_START", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Checking payment status - enabling payment module");
                mode_payment = true;
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
                    safe_ble_send_misc_data(tmp_uid_msg);
                } else {
                    ESP_LOGI(TAG, "No payment card detected");
                    safe_ble_send_misc_data("[PAY] NO_CARD");
                }
                break;
            }
            else if(strcmp("PROX_READ", data) == 0) {
                #if ENABLE_PROXIMITY_SENSOR
                ESP_LOGI(TAG, "BLE Command: Reading proximity sensor value");
                uint8_t proximity_value = proximity_sensor_read(proximity_sensor);
                char proximity_str[32];
                snprintf(proximity_str, sizeof(proximity_str), "[PROX] %d", proximity_value);
                safe_ble_send_misc_data(proximity_str);
                #else
                ESP_LOGI(TAG, "Proximity Sensor is DISABLED - cannot read value");
                safe_ble_send_misc_data("[ERROR] PROX_DISABLED");
                #endif
                break;
            }
            break;

        case 'I': // "IMU_" commands or "IV_" item verification commands
            if(strcmp("IMU_CHECK_ACTIVITY", data) == 0 || strcmp("IMU_STATUS", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Checking IMU activity");
                if(icm20948_is_moving(&imu_sensor)) {
                    ESP_LOGI(TAG, "IMU reports: Cart is moving");
                    safe_ble_send_misc_data("[IMU] MOVING");
                } else {
                    if(imu_sensor.idle_counter_ms >= IMU_IDLE_TIME_MINUTES * 60 * 1000){
                        ESP_LOGI(TAG, "IMU reports: Cart has been idle for %d minutes", IMU_IDLE_TIME_MINUTES);
                        safe_ble_send_misc_data("[IMU] IDLE");
                        return;
                    }
                    else{
                        ESP_LOGI(TAG, "IMU reports: Cart has been stopped");
                        safe_ble_send_misc_data("[IMU] STOPPED");
                    }
                }
            }
            else if(strcmp("IMU_IDLE_TIME", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Getting IMU idle time");
                uint32_t idle_time_ms = imu_sensor.idle_counter_ms;
                char idle_time_str[64];
                snprintf(idle_time_str, sizeof(idle_time_str), "[IMU] IDLE_TIME: %lu", idle_time_ms);
                safe_ble_send_misc_data(idle_time_str);
            }
            else if(strcmp("IMU_ACCEL", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Getting IMU acceleration");
                icm20948_read_accel(&imu_sensor);
                char accel_str[64];
                snprintf(accel_str, sizeof(accel_str), "[IMU] ACCEL: X=%.2f, Y=%.2f, Z=%.2f",
                         imu_sensor.accel.x, imu_sensor.accel.y, imu_sensor.accel.z);
                safe_ble_send_misc_data(accel_str);
            }
            else if(strcmp("IMU_HEADING", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Getting IMU heading");
                float heading = icm20948_compute_heading(&imu_sensor);
                char heading_str[32];
                snprintf(heading_str, sizeof(heading_str), "[IMU] HEADING: %.2f", heading);
                safe_ble_send_misc_data(heading_str);
            }
            else if(strcmp("IV_TRIG", data) == 0 || strcmp("IV_SCAN", data) == 0) {
                ESP_LOGI(TAG, "BLE Command: Force triggering item scan");

                #if ENABLE_ITEM_VERIFICATION
                item_rfid_scan(item_reader); // callback at on_item_scan_complete()
                #else
                ESP_LOGI(TAG, "Item Verification is DISABLED - cannot perform item scan");
                safe_ble_send_misc_data("[ERROR] IV_DISABLED");
                #endif

                break;
            }
            break;
        
        case 'C': // Cart Tracking - txt file commands
            safe_ble_send_misc_data("[IMU] Moving");

            if(strcmp("CT_START", data) == 0) {
                load_cell_tare(cart_load_cell);
                ESP_LOGI(TAG, "Load cell tared for tracking");
                
                #if ENABLE_CART_TRACKING
                ESP_LOGI(TAG, "Starting cart tracking data logging");
                startSession();

                #if ENABLE_ITEM_VERIFICATION
                item_rfid_scan(item_reader);
                ESP_LOGI(TAG, "Initial item scan performed for tracking session");
                #else
                ESP_LOGI(TAG, "Item Verification is DISABLED - skipping initial item scan for tracking session");
                #endif

                xTaskCreate(item_verification_task, "item_verification", 8192, NULL, IV_TASK_PRIORITY, &item_verification_task_handle);
                ESP_LOGI(TAG, "Item verification task created (interval: %d ms)", ITEM_VERIFICATION_INTERVAL_MS);

                mode_cart_tracking = true;
                xTaskCreate(cart_tracking_task, "cart_tracking", 8192, NULL, CT_TASK_PRIORITY, &cart_tracking_task_handle);
                ESP_LOGI(TAG, "Cart tracking task created");

                #else
                ESP_LOGI(TAG, "Cart Tracking is DISABLED - cannot start tracking session");
                safe_ble_send_misc_data("[ERROR] CT_DISABLED");
                #endif
            }
            else if(strcmp("CT_STOP", data) == 0) {

                #if ENABLE_CART_TRACKING
                ESP_LOGI(TAG, "Stopping cart tracking data logging");
                ESP_LOGI(TAG, "Exporting cart tracking data log via BLE");

                mode_cart_tracking = false;
                if(cart_tracking_task_handle != NULL){
                    vTaskDelete(cart_tracking_task_handle);
                    cart_tracking_task_handle = NULL;
                    ESP_LOGI(TAG, "Cart tracking task stopped");
                }

                if (item_verification_task_handle != NULL) {
                    vTaskDelete(item_verification_task_handle);
                    item_verification_task_handle = NULL;
                    ESP_LOGI(TAG, "Item verification task stopped");
                }

                endSession(true);
                #else
                ESP_LOGI(TAG, "Cart Tracking is DISABLED - cannot stop tracking session");
                safe_ble_send_misc_data("[ERROR] CT_DISABLED");
                #endif
            }
            else if(strcmp("CT_CLEAR", data) == 0) {

                #if ENABLE_CART_TRACKING
                ESP_LOGI(TAG, "Clearing cart tracking data log");

                // Disable cart tracking
                mode_cart_tracking = false;
                if(cart_tracking_task_handle != NULL){
                    vTaskDelete(cart_tracking_task_handle);
                    cart_tracking_task_handle = NULL;
                    ESP_LOGI(TAG, "Cart tracking task stopped");
                }

                // Stop item verification
                if (item_verification_task_handle != NULL) {
                    vTaskDelete(item_verification_task_handle);
                    item_verification_task_handle = NULL;
                    ESP_LOGI(TAG, "Item verification task stopped");
                }

                // End session and remove file without sending
                endSession(false);
                #else
                ESP_LOGI(TAG, "Cart Tracking is DISABLED - cannot clear tracking session");
                safe_ble_send_misc_data("[ERROR] CT_DISABLED");
                #endif
            }
            break;
        case 'O':
            if(strcmp("OUTDOOR_MODE_ON", data) == 0){
                ESP_LOGI(TAG, "BLE Command: Switching to OUTDOOR mode");
                outdoor_setting();
                mode_outdoor = true;
                safe_ble_send_misc_data("[MODE] OUTDOOR MODE ON");
                return;
            }
            break;
        default:
            ESP_LOGW(TAG, "BLE Command: Unknown command '%c' (full: %s)", data[0], data);
            safe_ble_send_misc_data("[ERROR] UNKNOWN_CMD");
            break;
    }
}

// ===== Individual Setup Functions =====

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

static void i2c_setup(void)
{
    ESP_LOGI(TAG, "Initializing I2C bus...");
    i2c_master_bus_config_t i2c_bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,  // Use internal pull-ups as fallback
        },
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_handle));
    ESP_LOGI(TAG, "I2C master bus initialized");
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
    xTaskCreate(icm20948_monitor_task, "imu_monitor", 4096, &imu_sensor, IMU_TASK_PRIORITY, &imu_monitor_task_handle);
    ESP_LOGI(TAG, "IMU monitoring task created (5-minute idle timeout)");
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
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PROXIMITY_INT_PIN, proximity_isr, NULL);
    ESP_LOGI(TAG, "Proximity interrupt ready on GPIO %d", PROXIMITY_INT_PIN);
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

// ===== ISRs =====
static void IRAM_ATTR button_isr(void *arg)
{
    uint32_t now_ms = esp_timer_get_time() / 1000;
    uint32_t time_since_last = now_ms - last_button_isr_time_ms;

    if (time_since_last >= BUTTON_COOLDOWN_MS) {
        last_button_isr_time_ms = now_ms;
        uint32_t evt = 1;
        xQueueSendFromISR(button_evt_queue, &evt, NULL);
        ESP_EARLY_LOGI(TAG, "Button interrupt triggered");
    }
}

static void IRAM_ATTR proximity_isr(void *arg)
{      
    uint32_t now_ms = esp_timer_get_time() / 1000;
    uint32_t time_since_last = now_ms - last_proximity_isr_time_ms;

    ESP_EARLY_LOGI(TAG, "Proximity ISR triggered at %u ms", now_ms);

    if (time_since_last >= PROX_COOLDOWN_MS) {
        last_proximity_isr_time_ms = now_ms;
        uint32_t evt = 1;
        xQueueSendFromISR(proximity_evt_queue, &evt, NULL);
        ESP_EARLY_LOGI(TAG, "Proximity interrupt triggered");
    }
}

// RTOS Tasks...
static void icm20948_monitor_task(void *arg)
{
    ICM20948_t *imu = (ICM20948_t *)arg;

    ESP_LOGI(TAG, "IMU monitor task started");

    while (1) {
        icm20948_activity_task(imu);

        vTaskDelay(pdMS_TO_TICKS(IMU_MONITOR_INTERVAL_MS));
    }
}

static void item_verification_task(void *arg)
{
    ESP_LOGI(TAG, "Item verification task started (periodic RFID scan every %d ms)", ITEM_VERIFICATION_INTERVAL_MS);

    while (1) {
        #if ENABLE_ITEM_VERIFICATION
        if (!item_rfid_is_scanning(item_reader)) {
            esp_err_t scan_ret = item_rfid_scan(item_reader);
            if (scan_ret == ESP_OK) {
                ESP_LOGI(TAG, "✓ Periodic RFID scan triggered successfully");
            } else {
                ESP_LOGW(TAG, "✗ Failed to trigger periodic RFID scan (error: %d)", scan_ret);
            }
        } else {
            ESP_LOGD(TAG, "RFID scan already in progress, skipping trigger");
        }
        #else
        ESP_LOGI(TAG, "Item Verification is DISABLED - skipping item scan");
        #endif

        vTaskDelay(pdMS_TO_TICKS(ITEM_VERIFICATION_INTERVAL_MS));
    }
}

static void cart_tracking_task(void *arg)
{
    ESP_LOGI(TAG, "Cart tracking task started (%.1f second interval)", CART_TRACKING_INTERVAL_MS / 1000.0f);

    while (1) {
        if (mode_cart_tracking) {
            BurstRead_CartTracking();
            ESP_LOGI(TAG, "Cart tracking burst read completed");
        }

        // Delay by the configured interval
        vTaskDelay(pdMS_TO_TICKS(CART_TRACKING_INTERVAL_MS));
    }
}

// Other callback functions...
void on_item_scan_complete(const item_rfid_tag_t *tags, int count) {
    ESP_LOGI(TAG, "Found %d items in cart", count);

    float cart_weight = load_cell_display_pounds(cart_load_cell);

    char verification_msg[512] = {0};
    int offset = snprintf(verification_msg, sizeof(verification_msg), "%.4f,%d", cart_weight, count);

    for (int i = 0; i < count && offset < (int)sizeof(verification_msg) - 1; i++) {
        offset += snprintf(verification_msg + offset,
                          sizeof(verification_msg) - offset,
                          ",%s",
                          tags[i].tag);
    }

    esp_err_t send_ret = ble_send_item_verification(verification_msg);
    if (send_ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ Cart verification sent via BLE: %s", verification_msg);
    } else {
        ESP_LOGW(TAG, "✗ Failed to send cart verification via BLE");
    }
}

static void handle_imu_idle_event(void)
{
    ESP_LOGI(TAG, "⏱ IMU: Cart idle for 5 minutes - no motion detected");
    safe_ble_send_misc_data("[IMU] IDLE");
}

static void handle_imu_motion_after_idle_event(void)
{
    ESP_LOGI(TAG, "🚀 IMU: Motion detected after 5+ minute idle");
    safe_ble_send_misc_data("[IMU] Moving");
}

// Helper function to safely send misc data, checking if cart tracking transfer is active
static void safe_ble_send_misc_data(const char *data)
{
    if (!is_cart_tracking_transfer_active()) {
        ble_send_misc_data(data);
    } else {
        // Silently skip - transfer in progress
        // ESP_LOGD(TAG, "Skipping misc data send - cart tracking transfer active");
    }
}

// ===== Cart Outside =====
static void outdoor_setting(){
    ESP_LOGI(TAG, "Setting cart for outdoor use...");
    ESP_LOGI(TAG, "Disabling all components except BLE...");

    // Stop item verification task if running
    if (item_verification_task_handle != NULL) {
        vTaskDelete(item_verification_task_handle);
        item_verification_task_handle = NULL;
        ESP_LOGI(TAG, "Item verification task stopped");
    }

    // Stop IMU monitoring task
    if (imu_monitor_task_handle != NULL) {
        vTaskDelete(imu_monitor_task_handle);
        imu_monitor_task_handle = NULL;
        ESP_LOGI(TAG, "IMU monitoring task stopped");
    }

    // Stop cart tracking
    #if ENABLE_CART_TRACKING
    mode_cart_tracking = false;
    if(cart_tracking_task_handle != NULL){
        vTaskDelete(cart_tracking_task_handle);
        cart_tracking_task_handle = NULL;
        ESP_LOGI(TAG, "Cart tracking task stopped");
    }

    ESP_LOGI(TAG, "Cart tracking disabled");
    #endif

    // Disable proximity sensor interrupt
    if (proximity_sensor != NULL) {
        proximity_sensor_disable_interrupt(proximity_sensor);
        ESP_LOGI(TAG, "Proximity sensor interrupt disabled");
    }

    // Disable proximity GPIO interrupt
    gpio_isr_handler_remove(PROXIMITY_INT_PIN);
    ESP_LOGI(TAG, "Proximity GPIO interrupt disabled");

    // Disable button interrupt
    gpio_isr_handler_remove(BUTTON_PIN);
    ESP_LOGI(TAG, "Button interrupt disabled");

    // Disable barcode scanner
    barcode_set_manual_mode(&barcanner);
    ESP_LOGI(TAG, "Barcode scanner disabled");

    // Disable payment mode
    mode_payment = false;
    ESP_LOGI(TAG, "Payment mode disabled");

    // Disable item verification (RFID)
    #if ENABLE_ITEM_VERIFICATION
    if (item_reader != NULL) {
        item_rfid_deinit(item_reader);
        item_reader = NULL;
        ESP_LOGI(TAG, "Item RFID reader disabled");
    }
    #endif

    ESP_LOGI(TAG, "✓ Outdoor mode activated - BLE only");
}

// ===== Cart Inside =====
static void indoor_setting(){
    ESP_LOGI(TAG, "Setting cart for indoor use...");
    ESP_LOGI(TAG, "Re-enabling all components...");

    // Re-enable IMU monitoring task
    if (imu_monitor_task_handle == NULL) {
        xTaskCreate(icm20948_monitor_task, "imu_monitor", 4096, &imu_sensor, IMU_TASK_PRIORITY, &imu_monitor_task_handle);
        ESP_LOGI(TAG, "IMU monitoring task re-enabled");
    }

    // Re-enable barcode scanner
    barcode_set_manual_mode(&barcanner);
    ESP_LOGI(TAG, "Barcode scanner re-enabled");

    // Re-enable button interrupt
    gpio_isr_handler_add(BUTTON_PIN, button_isr, NULL);
    ESP_LOGI(TAG, "Button interrupt re-enabled");

    // Re-enable proximity sensor interrupt
    if (proximity_sensor != NULL) {
        proximity_sensor_enable_interrupt(proximity_sensor);
        ESP_LOGI(TAG, "Proximity sensor interrupt re-enabled");
    }

    // Re-enable proximity GPIO interrupt
    gpio_isr_handler_add(PROXIMITY_INT_PIN, proximity_isr, NULL);
    ESP_LOGI(TAG, "Proximity GPIO interrupt re-enabled");

    // Re-enable item verification (RFID)
    #if ENABLE_ITEM_VERIFICATION
    if (item_reader == NULL) {
        item_reader = item_rfid_init(
            ITEM_RFID_UART_PORT,
            ITEM_RFID_TX_PIN,
            ITEM_RFID_RX_PIN,
            on_item_scan_complete
        );
        ESP_LOGI(TAG, "Item RFID reader re-enabled");
    }
    #endif

    ESP_LOGI(TAG, "✓ Indoor mode activated - all components enabled");
}

// ==== Payment helper ====
bool try_payment(uint8_t *uid, uint8_t uid_len){
    uint8_t authorized_uid[] = AUTHORIZED_UID;
    bool match = (uid_len == AUTHORIZED_UID_LEN);
    for (int i = 0; i < AUTHORIZED_UID_LEN && match; i++) {
        if (uid[i] != authorized_uid[i]) match = false;
    }

    if (match) {
        printf("💳 Payment Successful!\n");
        ble_send_payment_status("1");
        return true;
    } else {
        printf("🚫 Payment Declined. Try another card.\n");
        ble_send_payment_status("0");
        return false;
    }
    return false;
}

// ===== Debug LED Blink =====
static void debug_led(){

    ESP_LOGI(TAG, "Initializing debug LED on GPIO 21...");
    gpio_config_t debug_led_conf = {
        .pin_bit_mask = 1ULL << DEBUG_LED_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&debug_led_conf);
    gpio_set_level(DEBUG_LED_PIN, 0); 
    
    for(int i = 0; i < 4; i++){
        ESP_LOGI(TAG, "Testing GPIO 21 output... blink #%d", i);

        gpio_set_level(DEBUG_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(DEBUG_LED_PIN, 0); 
        vTaskDelay(pdMS_TO_TICKS(200));

    }

    return;
}
