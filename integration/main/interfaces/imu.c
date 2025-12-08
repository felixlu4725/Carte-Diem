#include "imu.h"
#include "math.h"
#include "esp_log.h"

#define TAG "ICM20948"

// Low-pass-ish state for motion detection
static float accel_magnitude_prev = 0.0f;

/* -------------------------------------------------------------------------- */
/* Timer callback for activity monitor                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Timer callback for activity monitoring
 */
static void activity_timer_callback(TimerHandle_t xTimer)
{
    ICM20948_t *dev = (ICM20948_t *)pvTimerGetTimerID(xTimer);
    // Send idle event to queue if it exists (avoids blocking Timer Service task)
    if (dev->idle_event_queue) {
        uint32_t idle_event = 1;
        xQueueSendFromISR(dev->idle_event_queue, &idle_event, NULL);
    }
}

/* -------------------------------------------------------------------------- */
/* I2C helpers                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read multiple bytes from ICM20948 register
 */
static esp_err_t icm20948_read_bytes(ICM20948_t *device, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(device->dev_handle, &reg, 1, data, len, -1);
}

/**
 * @brief Write single byte to ICM20948 register
 */
static esp_err_t icm20948_write_byte(ICM20948_t *device, uint8_t reg, uint8_t value)
{
    uint8_t write_buf[2] = { reg, value };
    return i2c_master_transmit(device->dev_handle, write_buf, sizeof(write_buf), -1);
}

/**
 * @brief Read 16-bit signed value from ICM20948
 */
static int16_t icm20948_read_int16(ICM20948_t *device, uint8_t reg_high)
{
    uint8_t data[2];
    icm20948_read_bytes(device, reg_high, data, 2);
    return (int16_t)((data[0] << 8) | data[1]);
}

/* -------------------------------------------------------------------------- */
/* Bank select                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Select register bank on ICM20948
 */
esp_err_t icm20948_select_bank(ICM20948_t *dev, uint8_t bank)
{
    return icm20948_write_byte(dev, ICM20948_REG_BANK_SEL, bank);
}

/* -------------------------------------------------------------------------- */
/* Magnetometer init (AK09916 behind ICM-20948 I2C master)                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize AK09916 magnetometer
 */
esp_err_t icm20948_init_mag(ICM20948_t *dev)
{
    esp_err_t ret;

    // --- Step 1: Enable I2C master (Bank 0, USER_CTRL) ---
    icm20948_select_bank(dev, ICM20948_BANK_0);
    ret = icm20948_write_byte(dev, REG_USER_CTRL, USER_CTRL_I2C_MST_ENABLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2C master (USER_CTRL)");
        return ret;
    }

    // --- Step 2: Configure I2C master clock (Bank 3, I2C_MST_CTRL) ---
    icm20948_select_bank(dev, ICM20948_BANK_3);
    ret = icm20948_write_byte(dev, REG_I2C_MST_CTRL, 0x07); // ~400 kHz
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set I2C_MST_CTRL");
        return ret;
    }

    // --- Step 3: Use SLV1 to write AK09916 CNTL2 = 100 Hz continuous mode ---
    // NOTE: For ICM-20948, I2C_SLVx_ADDR uses:
    //   bit7 = R/W, bits[6:0] = 7-bit address
    //   So: write = 0x0C, read = 0x8C
    ret  = icm20948_write_byte(dev, REG_I2C_SLV1_ADDR, AK09916_I2C_ADDR);          // Write mode, R/W=0
    ret |= icm20948_write_byte(dev, REG_I2C_SLV1_REG,  AK09916_REG_CNTL2);
    ret |= icm20948_write_byte(dev, REG_I2C_SLV1_DO,   AK09916_MODE_100HZ);
    ret |= icm20948_write_byte(dev, REG_I2C_SLV1_CTRL, 0x81);  // enable, 1 byte
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2C_SLV1 for CNTL2 write");
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(20)); // Allow magnetometer to enter mode

    // --- Step 4: Configure SLV0 to read 8 bytes starting at ST1 ---
    ret  = icm20948_write_byte(dev, REG_I2C_SLV0_ADDR, AK09916_I2C_ADDR | 0x80); // Read mode, R/W=1
    ret |= icm20948_write_byte(dev, REG_I2C_SLV0_REG,  AK09916_REG_ST1);
    ret |= icm20948_write_byte(dev, REG_I2C_SLV0_CTRL, 0x88);  // enable, 8 bytes
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2C_SLV0 for mag read");
        return ret;
    }

    // Back to Bank 0
    icm20948_select_bank(dev, ICM20948_BANK_0);

    ESP_LOGI(TAG, "Magnetometer initialized (AK09916 @ 100Hz)");
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Main IMU init                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize ICM20948 IMU sensor
 */
void icm20948_init(ICM20948_t *device, i2c_master_bus_handle_t bus_handle)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ICM20948_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &device->dev_handle));

    // Default sensitivities (will be updated by set_* functions)
    device->accel.sensitivity = 16384.0f; // ±2g
    device->gyro.sensitivity  = 131.0f;   // ±250 dps
    device->mag.sensitivity   = AK09916_SENSITIVITY; // µT/LSB

    device->status           = STANDBY;
    device->direction_deg    = 0.0f;
    device->idle_counter_ms  = 0;
    device->activity_timer   = NULL;
    device->idle_event_queue = NULL;
    device->last_queue_send_ms = 0;
    device->first_queue_send_done = false;
    device->was_idle_long = false;
    device->motion_after_idle_queue = NULL;

    // --- Full chip reset ---
    icm20948_write_byte(device, ICM20948_PWR_MGMT_1, 0x80);  // DEVICE_RESET
    vTaskDelay(pdMS_TO_TICKS(100));

    // --- Wake up, select best clock (auto PLL) ---
    icm20948_write_byte(device, ICM20948_PWR_MGMT_1, 0x01);  // CLKSEL=1, SLEEP=0
    vTaskDelay(pdMS_TO_TICKS(10));

    // --- Enable accel + gyro on all axes ---
    icm20948_write_byte(device, ICM20948_PWR_MGMT_2, 0x00);

    // --- Configure full scale ranges (Bank 2) ---
    icm20948_set_gyroDPS(device, 250); // ±250 dps
    icm20948_set_accelG(device, 2);    // ±2g

    // WHO_AM_I check
    uint8_t who = 0;
    icm20948_read_bytes(device, ICM20948_WHO_AM_I, &who, 1);
    if (who != 0xEA) {
        ESP_LOGE(TAG, "ICM20948 WHO_AM_I mismatch: 0x%02X (expected 0xEA)", who);
    } else {
        ESP_LOGI(TAG, "ICM20948 detected (WHO_AM_I = 0x%02X)", who);
    }

    // Init mag (AK09916)
    icm20948_init_mag(device);

    ESP_LOGI(TAG, "ICM20948 initialized and sensors enabled");
}

/* -------------------------------------------------------------------------- */
/* Heading computation                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Compute heading from magnetometer and accelerometer
 */
float icm20948_compute_heading(ICM20948_t *dev)
{
    icm20948_read_mag(dev);
    icm20948_read_accel(dev);

    // If magnetometer is not actually returning data, don't pretend we have a heading
    if (dev->mag.x == 0.0f && dev->mag.y == 0.0f && dev->mag.z == 0.0f) {
        ESP_LOGW(TAG, "Magnetometer not configured – heading is invalid");
        return dev->direction_deg; // last known, or 0 on first call
    }

    float pitch = atan2f(-dev->accel.x,
                         sqrtf(dev->accel.y * dev->accel.y +
                               dev->accel.z * dev->accel.z));
    float roll  = atan2f(dev->accel.y, dev->accel.z);

    float xh = dev->mag.x * cosf(pitch) + dev->mag.z * sinf(pitch);
    float yh = dev->mag.x * sinf(roll) * sinf(pitch)
             + dev->mag.y * cosf(roll)
             - dev->mag.z * sinf(roll) * cosf(pitch);

    float heading = atan2f(yh, xh) * 180.0f / M_PI;
    if (heading < 0) heading += 360.0f;
    dev->direction_deg = heading;
    return heading;
}

/* -------------------------------------------------------------------------- */
/* Motion detection                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Check if device is moving based on acceleration
 */
bool icm20948_is_moving(ICM20948_t *dev)
{
    icm20948_read_accel(dev);
    float mag = sqrtf(dev->accel.x * dev->accel.x +
                      dev->accel.y * dev->accel.y +
                      dev->accel.z * dev->accel.z);
    float diff = fabsf(mag - accel_magnitude_prev);
    accel_magnitude_prev = mag;

    bool moving = (diff > IMU_MOVING_THRESHOLD); // ~30 mg threshold
    dev->status = moving ? MOVING : IDLE;
    return moving;
}

/* -------------------------------------------------------------------------- */
/* Activity monitor                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Track activity and detect idle state
 */
void icm20948_activity_task(ICM20948_t *dev)
{
    if (!icm20948_is_moving(dev)) {
        dev->idle_counter_ms += IMU_MONITOR_INTERVAL_MS;
        if (dev->idle_counter_ms >= IMU_IDLE_TIME_MINUTES * 60 * 1000) {
            dev->was_idle_long = true;  // Mark that we've been idle for 5+ minutes

            // Send idle event to queue (non-blocking from task context)
            if (dev->idle_event_queue) {
                uint32_t idle_event = 1;

                // First queue send
                if (!dev->first_queue_send_done) {
                    ESP_LOGI(TAG, "IMU idle threshold reached (%u ms) - sending event", dev->idle_counter_ms);
                    xQueueSend(dev->idle_event_queue, &idle_event, 0);
                    dev->first_queue_send_done = true;
                    dev->last_queue_send_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    ESP_LOGI(TAG, "First queue send completed");
                } else {
                    // Subsequent sends: only send every 1 minute (60000 ms)
                    uint32_t current_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    uint32_t time_since_last_send = current_ms - dev->last_queue_send_ms;

                    if (time_since_last_send >= 60000) {  // 1 minute = 60000 ms
                        ESP_LOGI(TAG, "IMU idle threshold reached (%u ms) - sending event", dev->idle_counter_ms);
                        xQueueSend(dev->idle_event_queue, &idle_event, 0);
                        dev->last_queue_send_ms = current_ms;
                        ESP_LOGI(TAG, "Queue send repeated (1 min throttle)");
                    }
                }
            }
        }
    } else {
        // Motion detected
        if (dev->was_idle_long) {
            // IMU was idle for 5+ minutes and motion just resumed (IMU_MONITOR_INTERVAL_MS)
            ESP_LOGI(TAG, "Motion detected after %u minute idle - sending motion event", IMU_MONITOR_INTERVAL_MS);
            if (dev->motion_after_idle_queue) {
                uint32_t motion_event = 1;
                xQueueSend(dev->motion_after_idle_queue, &motion_event, 0);
            }
            dev->was_idle_long = false;  // Reset the flag
        }

        dev->idle_counter_ms = 0;
        dev->first_queue_send_done = false;  // Reset when movement detected
        ESP_LOGI(TAG, "Movement detected - resetting idle counter");
    }
}

/* -------------------------------------------------------------------------- */
/* Accel / Gyro read                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read accelerometer data
 */
esp_err_t icm20948_read_accel(ICM20948_t *device)
{
    icm20948_select_bank(device, ICM20948_BANK_0);
    int16_t ax_raw = icm20948_read_int16(device, ICM20948_ACCEL_XOUT_H);
    int16_t ay_raw = icm20948_read_int16(device, ICM20948_ACCEL_YOUT_H);
    int16_t az_raw = icm20948_read_int16(device, ICM20948_ACCEL_ZOUT_H);

    device->accel.x = (float)ax_raw / device->accel.sensitivity;
    device->accel.y = (float)ay_raw / device->accel.sensitivity;
    device->accel.z = (float)az_raw / device->accel.sensitivity;

    return ESP_OK;
}

/**
 * @brief Read gyroscope data
 */
esp_err_t icm20948_read_gyro(ICM20948_t *device)
{
    icm20948_select_bank(device, ICM20948_BANK_0);
    int16_t gx_raw = icm20948_read_int16(device, ICM20948_GYRO_XOUT_H);
    int16_t gy_raw = icm20948_read_int16(device, ICM20948_GYRO_YOUT_H);
    int16_t gz_raw = icm20948_read_int16(device, ICM20948_GYRO_ZOUT_H);

    device->gyro.x = (float)gx_raw / device->gyro.sensitivity;
    device->gyro.y = (float)gy_raw / device->gyro.sensitivity;
    device->gyro.z = (float)gz_raw / device->gyro.sensitivity;

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Magnetometer read (EXT_SENS_DATA_00..07 -> AK09916)                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read magnetometer data
 */
esp_err_t icm20948_read_mag(ICM20948_t *dev)
{
    icm20948_select_bank(dev, ICM20948_BANK_0);

    uint8_t raw[8] = {0};
    esp_err_t ret = icm20948_read_bytes(dev, EXT_SENS_DATA_00, raw, 8);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read EXT_SENS_DATA: %d", ret);
        return ret;
    }

    // raw[0] = ST1, raw[1..6] = HXL..HZH
    // We ignore ST1/ST2 status here for now.
    int16_t mx = (int16_t)((raw[2] << 8) | raw[1]);
    int16_t my = (int16_t)((raw[4] << 8) | raw[3]);
    int16_t mz = (int16_t)((raw[6] << 8) | raw[5]);

    dev->mag.x = (float)mx * AK09916_SENSITIVITY;
    dev->mag.y = (float)my * AK09916_SENSITIVITY;
    dev->mag.z = (float)mz * AK09916_SENSITIVITY;

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Full-scale settings                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Set gyroscope full scale range (dps)
 */
esp_err_t icm20948_set_gyroDPS(ICM20948_t *device, uint32_t dps)
{
    uint8_t fs_sel = 0;
    switch (dps) {
        case 250:  fs_sel = 0; device->gyro.sensitivity = 131.0f;   break;
        case 500:  fs_sel = 1; device->gyro.sensitivity = 65.5f;    break;
        case 1000: fs_sel = 2; device->gyro.sensitivity = 32.8f;    break;
        case 2000: fs_sel = 3; device->gyro.sensitivity = 16.4f;    break;
        default:   return ESP_FAIL;
    }

    icm20948_select_bank(device, ICM20948_BANK_2);
    icm20948_write_byte(device, ICM20948_GYRO_CONFIG_1, (fs_sel << 1));
    icm20948_select_bank(device, ICM20948_BANK_0);
    return ESP_OK;
}

/**
 * @brief Set accelerometer full scale range (g)
 */
esp_err_t icm20948_set_accelG(ICM20948_t *device, uint8_t g)
{
    uint8_t fs_sel = 0;
    switch (g) {
        case 2:  fs_sel = 0; device->accel.sensitivity = 16384.0f;  break;
        case 4:  fs_sel = 1; device->accel.sensitivity = 8192.0f;   break;
        case 8:  fs_sel = 2; device->accel.sensitivity = 4096.0f;   break;
        case 16: fs_sel = 3; device->accel.sensitivity = 2048.0f;   break;
        default: return ESP_FAIL;
    }

    icm20948_select_bank(device, ICM20948_BANK_2);
    icm20948_write_byte(device, ICM20948_ACCEL_CONFIG, (fs_sel << 1));
    icm20948_select_bank(device, ICM20948_BANK_0);
    return ESP_OK;
}
