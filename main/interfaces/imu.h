#pragma once
#include "imu_defs.h"
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
} ICM20948_t;

void icm20948_init(ICM20948_t *device, i2c_master_bus_handle_t bus_handle);
esp_err_t icm20948_read_accel(ICM20948_t *device);
esp_err_t icm20948_read_gyro(ICM20948_t *device);
esp_err_t icm20948_read_mag(ICM20948_t *device);
esp_err_t icm20948_set_gyroDPS(ICM20948_t *device, uint32_t dps);
esp_err_t icm20948_set_accelG(ICM20948_t *device, uint8_t g);

// New functions
float icm20948_compute_heading(ICM20948_t *device);
bool icm20948_is_moving(ICM20948_t *device);
void icm20948_start_activity_monitor(ICM20948_t *device, QueueHandle_t idle_queue);
void icm20948_activity_task(ICM20948_t *device);
esp_err_t icm20948_select_bank(ICM20948_t *dev, uint8_t bank);
esp_err_t icm20948_init_mag(ICM20948_t *dev);
