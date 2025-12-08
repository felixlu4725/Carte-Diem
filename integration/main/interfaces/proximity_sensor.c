#include "proximity_sensor.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "ProximitySensor";

// Helper function declarations
static bool proximity_sensor_write_register(ProximitySensor* sensor, uint8_t reg, uint8_t value);
static bool proximity_sensor_read_register(ProximitySensor* sensor, uint8_t reg, uint8_t* value);
static bool proximity_sensor_write_register_bit(ProximitySensor* sensor, uint8_t reg, uint8_t bit_mask, bool value);

// Public function implementations

/**
 * @brief Create proximity sensor instance
 */
ProximitySensor* proximity_sensor_create(uint8_t int_pin, uint8_t threshold, bool verbose) {
    ProximitySensor* sensor = (ProximitySensor*)malloc(sizeof(ProximitySensor));
    if (sensor == NULL) {
        return NULL;
    }

    sensor->dev_handle = NULL;
    sensor->int_pin = int_pin;
    sensor->threshold = threshold;
    sensor->verbose = verbose;
    sensor->connected = false;

    return sensor;
}

/**
 * @brief Destroy proximity sensor instance
 */
void proximity_sensor_destroy(ProximitySensor* sensor) {
    if (sensor == NULL) {
        return;
    }

    // Device handle cleanup is handled by the bus, not here
    free(sensor);
}

/**
 * @brief Initialize proximity sensor on I2C bus
 */
bool proximity_sensor_begin(ProximitySensor* sensor, i2c_master_bus_handle_t bus_handle) {
    if (sensor == NULL || bus_handle == NULL) {
        return false;
    }

    // Configure I2C device on the shared bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = APDS9960_I2C_ADDR,
        .scl_speed_hz = 400000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &sensor->dev_handle);
    if (err != ESP_OK) {
        if (sensor->verbose) {
            ESP_LOGE(TAG, "Failed to add device to I2C bus: %s", esp_err_to_name(err));
        }
        return false;
    }

    // Give the sensor time to power up
    vTaskDelay(pdMS_TO_TICKS(50));

    // Check device ID
    uint8_t id = 0;
    if (!proximity_sensor_read_register(sensor, APDS9960_ID, &id)) {
        if (sensor->verbose) {
            ESP_LOGE(TAG, "Failed to read device ID");
        }
        return false;
    }

    if (id != APDS9960_ID_1 && id != APDS9960_ID_2) {
        if (sensor->verbose) {
            ESP_LOGE(TAG, "Invalid device ID: 0x%02X (expected 0x%02X or 0x%02X)",
                     id, APDS9960_ID_1, APDS9960_ID_2);
        }
        return false;
    }

    if (sensor->verbose) {
        ESP_LOGI(TAG, "Device ID: 0x%02X", id);
    }

    // Disable all features initially
    if (!proximity_sensor_write_register(sensor, APDS9960_ENABLE, 0x00)) {
        if (sensor->verbose) {
            ESP_LOGE(TAG, "Failed to disable features");
        }
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    // Power on the device
    if (!proximity_sensor_write_register(sensor, APDS9960_ENABLE, APDS9960_PON)) {
        if (sensor->verbose) {
            ESP_LOGE(TAG, "Failed to power on device");
        }
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));



    // Set default proximity gain (4x)
    if (!proximity_sensor_set_gain(sensor, APDS9960_PGAIN_4X)) {
        if (sensor->verbose) {
            ESP_LOGE(TAG, "Failed to set proximity gain");
        }
        return false;
    }

    // Set default LED drive (100mA)
    if (!proximity_sensor_set_led_drive(sensor, APDS9960_LED_DRIVE_100MA)) {
        if (sensor->verbose) {
            ESP_LOGE(TAG, "Failed to set LED drive");
        }
        return false;
    }

    // Set default LED boost (100%)
    uint8_t config2 = 0;
    if (!proximity_sensor_read_register(sensor, APDS9960_CONFIG2, &config2)) {
        return false;
    }
    config2 = (config2 & 0xCF) | APDS9960_LED_BOOST_100;
    if (!proximity_sensor_write_register(sensor, APDS9960_CONFIG2, config2)) {
        return false;
    }

    // Set default proximity pulse (8 pulses, 8us length)
    if (!proximity_sensor_set_pulse(sensor, APDS9960_PULSE_LEN_8US, 8)) {
        if (sensor->verbose) {
            ESP_LOGE(TAG, "Failed to set proximity pulse");
        }
        return false;
    }

    // Enable proximity mode
    uint8_t enable = 0;
    if (!proximity_sensor_read_register(sensor, APDS9960_ENABLE, &enable)) {
        return false;
    }
    enable |= (APDS9960_PON | APDS9960_PEN);
    if (!proximity_sensor_write_register(sensor, APDS9960_ENABLE, enable)) {
        if (sensor->verbose) {
            ESP_LOGE(TAG, "Failed to enable proximity");
        }
        return false;
    }

    // Set the interrupt threshold (low threshold = 0, high threshold = THRESHOLD)
    if (!proximity_sensor_write_register(sensor, APDS9960_PILT, 0)) {
        return false;
    }
    if (!proximity_sensor_write_register(sensor, APDS9960_PIHT, sensor->threshold)) {
        if (sensor->verbose) {
            ESP_LOGE(TAG, "Failed to set interrupt threshold");
        }
        return false;
    }

    if (sensor->verbose) {
        ESP_LOGI(TAG, "Interrupt when value < %d", sensor->threshold);
    }

    // Enable proximity interrupt
    proximity_sensor_enable_interrupt(sensor);
    proximity_sensor_clear_interrupt(sensor);

    if (sensor->verbose) {
        ESP_LOGI(TAG, "✓ Proximity sensor ready...");
    }

    sensor->connected = true;
    return true;
}

/**
 * @brief Check if sensor is connected and operational
 */
bool proximity_sensor_is_connected(const ProximitySensor* sensor) {
    if (sensor == NULL) {
        return false;
    }
    return sensor->connected;
}

/**
 * @brief Read proximity sensor value
 */
uint8_t proximity_sensor_read(ProximitySensor* sensor) {
    if (sensor == NULL) {
        return 0;
    }

    uint8_t proximity = 0;
    if (!proximity_sensor_read_register(sensor, APDS9960_PDATA, &proximity)) {
        if (sensor->verbose) {
            ESP_LOGE(TAG, "Failed to read proximity data");
        }
        return 0;
    }

    if (sensor->verbose) {
        ESP_LOGI(TAG, "Proximity value: %d", proximity);
    }

    return proximity;
}

/**
 * @brief Clear proximity sensor interrupt
 */
void proximity_sensor_clear_interrupt(ProximitySensor* sensor) {
    if (sensor == NULL || sensor->dev_handle == NULL) {
        return;
    }

    // Clear interrupt by writing to PICLEAR register
    uint8_t reg = APDS9960_PICLEAR;
    i2c_master_transmit(sensor->dev_handle, &reg, 1, -1);
}

/**
 * @brief Enable proximity sensor interrupt
 */
void proximity_sensor_enable_interrupt(ProximitySensor* sensor) {
    if (sensor == NULL) {
        return;
    }

    uint8_t enable = 0;
    if (proximity_sensor_read_register(sensor, APDS9960_ENABLE, &enable)) {
        enable |= APDS9960_PIEN;
        enable &= ~APDS9960_AIEN;
        proximity_sensor_write_register(sensor, APDS9960_ENABLE, enable);
        proximity_sensor_write_register(sensor, APDS9960_PERS, 0x40); 
    }
}

/**
 * @brief Disable proximity sensor interrupt
 */
void proximity_sensor_disable_interrupt(ProximitySensor* sensor) {
    if (sensor == NULL) {
        return;
    }

    uint8_t enable = 0;
    if (proximity_sensor_read_register(sensor, APDS9960_ENABLE, &enable)) {
        enable &= ~APDS9960_PIEN;
        proximity_sensor_write_register(sensor, APDS9960_ENABLE, enable);
    }
}

/**
 * @brief Set proximity sensor gain
 */
bool proximity_sensor_set_gain(ProximitySensor* sensor, uint8_t gain) {
    if (sensor == NULL) {
        return false;
    }

    uint8_t control = 0;
    if (!proximity_sensor_read_register(sensor, APDS9960_CONTROL, &control)) {
        return false;
    }

    // Clear proximity gain bits and set new value
    control = (control & 0xF3) | (gain & 0x0C);
    control = (control & 0xFC) | (APDS9960_AGAIN_4X & 0x03);
    return proximity_sensor_write_register(sensor, APDS9960_CONTROL, control);
}

/**
 * @brief Set proximity sensor LED drive strength
 */
bool proximity_sensor_set_led_drive(ProximitySensor* sensor, uint8_t drive) {
    if (sensor == NULL) {
        return false;
    }

    uint8_t control = 0;
    if (!proximity_sensor_read_register(sensor, APDS9960_CONTROL, &control)) {
        return false;
    }

    // Clear LED drive bits and set new value
    control = (control & 0x3F) | (drive & 0xC0);
    return proximity_sensor_write_register(sensor, APDS9960_CONTROL, control);
}

/**
 * @brief Configure proximity sensor pulse length and count
 */
bool proximity_sensor_set_pulse(ProximitySensor* sensor, uint8_t pulse_length, uint8_t pulse_count) {
    if (sensor == NULL) {
        return false;
    }

    if (pulse_count < 1 || pulse_count > 64) {
        return false;
    }

    // Pulse count is encoded as count - 1
    uint8_t ppulse = (pulse_length & 0xC0) | ((pulse_count - 1) & 0x3F);
    return proximity_sensor_write_register(sensor, APDS9960_PPULSE, ppulse);
}

// Helper function implementations

/**
 * @brief Write value to proximity sensor register
 */
static bool proximity_sensor_write_register(ProximitySensor* sensor, uint8_t reg, uint8_t value) {
    if (sensor == NULL || sensor->dev_handle == NULL) {
        return false;
    }

    uint8_t write_buf[2] = {reg, value};
    esp_err_t ret = i2c_master_transmit(sensor->dev_handle, write_buf, sizeof(write_buf), -1);

    if (ret != ESP_OK && sensor->verbose) {
        ESP_LOGE(TAG, "Failed to write register 0x%02X: %s", reg, esp_err_to_name(ret));
    }

    return (ret == ESP_OK);
}

/**
 * @brief Read value from proximity sensor register
 */
static bool proximity_sensor_read_register(ProximitySensor* sensor, uint8_t reg, uint8_t* value) {
    if (sensor == NULL || sensor->dev_handle == NULL || value == NULL) {
        return false;
    }

    esp_err_t ret = i2c_master_transmit_receive(sensor->dev_handle, &reg, 1, value, 1, -1);

    if (ret != ESP_OK && sensor->verbose) {
        ESP_LOGE(TAG, "Failed to read register 0x%02X: %s", reg, esp_err_to_name(ret));
    }

    return (ret == ESP_OK);
}

/**
 * @brief Write bit mask to proximity sensor register
 */
static bool proximity_sensor_write_register_bit(ProximitySensor* sensor, uint8_t reg, uint8_t bit_mask, bool value) {
    uint8_t reg_value = 0;
    if (!proximity_sensor_read_register(sensor, reg, &reg_value)) {
        return false;
    }

    if (value) {
        reg_value |= bit_mask;
    } else {
        reg_value &= ~bit_mask;
    }

    return proximity_sensor_write_register(sensor, reg, reg_value);
}