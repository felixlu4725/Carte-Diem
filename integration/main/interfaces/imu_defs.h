#pragma once

#include <stdint.h>

/* ============================================
 *   ICM20948 MAIN ADDRESS
 * ============================================ */
#define ICM20948_I2C_ADDR 0x68     // Most breakouts use AD0 = LOW → 0x68


/* ============================================
 *   REGISTER BANK SELECT
 * ============================================ */
#define ICM20948_BANK_0   0x00
#define ICM20948_BANK_1   0x10
#define ICM20948_BANK_2   0x20
#define ICM20948_BANK_3   0x30

#define ICM20948_REG_BANK_SEL 0x7F


/* ============================================
 *   BANK 0 REGISTERS
 * ============================================ */

// Power management
#define ICM20948_PWR_MGMT_1      0x06
#define ICM20948_PWR_MGMT_2      0x07

// User control
#define REG_USER_CTRL            0x03
#define USER_CTRL_I2C_MST_ENABLE 0x20    // Bit 5 enables internal I2C master

// Device ID
#define ICM20948_WHO_AM_I        0x00    // Should return 0xEA

// Sensor readings (Bank 0)
#define ICM20948_ACCEL_XOUT_H    0x2D
#define ICM20948_ACCEL_YOUT_H    0x2F
#define ICM20948_ACCEL_ZOUT_H    0x31

#define ICM20948_GYRO_XOUT_H     0x33
#define ICM20948_GYRO_YOUT_H     0x35
#define ICM20948_GYRO_ZOUT_H     0x37

// External Sensor Data (magnetometer bytes from AK09916)
#define EXT_SENS_DATA_00         0x3B


/* ============================================
 *   BANK 2 — CONFIG REGISTERS
 * ============================================ */

// Gyro configuration (Bank 2)
#define ICM20948_GYRO_CONFIG_1   0x01

// Accelerometer configuration (Bank 2)
#define ICM20948_ACCEL_CONFIG    0x14


/* ============================================
 *   BANK 3 — I2C MASTER CONTROL
 * ============================================ */
#define REG_I2C_MST_CTRL         0x01    // I2C Master Clock

#define REG_I2C_SLV0_ADDR        0x03
#define REG_I2C_SLV0_REG         0x04
#define REG_I2C_SLV0_CTRL        0x05

#define REG_I2C_SLV1_ADDR        0x07
#define REG_I2C_SLV1_REG         0x08
#define REG_I2C_SLV1_CTRL        0x09
#define REG_I2C_SLV1_DO          0x0A


/* ============================================
 *   AK09916 MAGNETOMETER (BEHIND ICM-20948)
 * ============================================ */

#define AK09916_I2C_ADDR         0x0C    // Fixed 7-bit address

// Identification
#define AK09916_REG_WIA          0x01    // Should be 0x09

// Status registers
#define AK09916_REG_ST1          0x10
#define AK09916_REG_ST2          0x18

// Magnetometer output data
#define AK09916_REG_HXL          0x11
#define AK09916_REG_HXH          0x12
#define AK09916_REG_HYL          0x13
#define AK09916_REG_HYH          0x14
#define AK09916_REG_HZL          0x15
#define AK09916_REG_HZH          0x16

// Control registers
#define AK09916_REG_CNTL2        0x31

// Operating modes
#define AK09916_MODE_POWERDOWN   0x00
#define AK09916_MODE_SINGLE      0x01
#define AK09916_MODE_10HZ        0x02
#define AK09916_MODE_20HZ        0x04
#define AK09916_MODE_50HZ        0x06
#define AK09916_MODE_100HZ       0x08     // Continuous Mode 4, 100 Hz


/* ============================================
 *   MISC CONSTANTS
 * ============================================ */
#define AK09916_SENSITIVITY      0.15f    // µT per LSB
