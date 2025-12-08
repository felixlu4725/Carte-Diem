#pragma once
#include "imu_defs.h"
#include "cartediem_defs.h"
#include "stdint.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"

enum IMUstatus {
    SLEEP,
    STANDBY,
    LOWPOWER,
    MOVING,
    IDLE
};

typedef struct {
    float x;
    float y;
    float z;
    float sensitivity;
} SensData_t;

typedef struct {
    SensData_t accel;
    SensData_t gyro;
    SensData_t mag;
    enum IMUstatus status;
    float direction_deg;  // facing direction (yaw)
    i2c_master_dev_handle_t dev_handle;

    // Activity tracking
    uint32_t idle_counter_ms;
    TimerHandle_t activity_timer;
    QueueHandle_t idle_event_queue;  // Queue to notify main task of idle events
    uint32_t last_queue_send_ms;     // Timestamp of last queue send (for 1-min throttling)
    bool first_queue_send_done;      // Flag to track first queue send
    bool was_idle_long;              // Flag to track if IMU was idle for 5+ minutes
    QueueHandle_t motion_after_idle_queue;  // Queue to notify main task when motion resumes after idle
} ICM20948_t;

/**
 * @brief Initialize ICM20948 IMU sensor
 */
void icm20948_init(ICM20948_t *device, i2c_master_bus_handle_t bus_handle);

/**
 * @brief Read accelerometer data
 */
esp_err_t icm20948_read_accel(ICM20948_t *device);

/**
 * @brief Read gyroscope data
 */
esp_err_t icm20948_read_gyro(ICM20948_t *device);

/**
 * @brief Read magnetometer data
 */
esp_err_t icm20948_read_mag(ICM20948_t *device);

/**
 * @brief Set gyroscope full scale range (dps)
 */
esp_err_t icm20948_set_gyroDPS(ICM20948_t *device, uint32_t dps);

/**
 * @brief Set accelerometer full scale range (g)
 */
esp_err_t icm20948_set_accelG(ICM20948_t *device, uint8_t g);

/**
 * @brief Compute heading from magnetometer and accelerometer
 */
float icm20948_compute_heading(ICM20948_t *device);

/**
 * @brief Check if device is moving based on acceleration
 */
bool icm20948_is_moving(ICM20948_t *device);

/**
 * @brief Check if device is moving fast
 */
bool icm20948_is_fast_moving(ICM20948_t *dev);

/**
 * @brief Track activity and detect idle state
 */
void icm20948_activity_task(ICM20948_t *device);

/**
 * @brief Select register bank on ICM20948
 */
esp_err_t icm20948_select_bank(ICM20948_t *dev, uint8_t bank);

/**
 * @brief Initialize AK09916 magnetometer
 */
esp_err_t icm20948_init_mag(ICM20948_t *dev);
