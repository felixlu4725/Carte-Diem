// cartediem_defs.h
// Check #1, 2, 3 settings at the top to configure
#pragma once

// 1. ENABLE FUNCTIONALITIES: 1 to enable, 0 to disable
#define ENABLE_LED_DEBUG_STARTUP 1
#define ENABLE_ITEM_VERIFICATION 1
#define ENABLE_CART_TRACKING 1
#define ENABLE_PROXIMITY_SENSOR 1

// 2. ADJUSTABLE PARAMETERS
#define BUTTON_COOLDOWN_MS 1000             // Button press cooldown time
#define PROX_COOLDOWN_MS 1000               // Proximity interrupt cooldown time
#define PROXIMITY_THRESHOLD 30              // Proximity sensor threshold value

// 2.1. RTOS PERIODIC TASK PARAMETERS
#define IV_TASK_PRIORITY 9
#define ITEM_VERIFICATION_INTERVAL_MS 10000  // Interval for periodic RFID scan for Item Verification

#define IMU_TASK_PRIORITY 7
#define IMU_MONITOR_INTERVAL_MS 15000        // 15 seconds
#define IMU_IDLE_TIME_MINUTES 5             // 1 minutes
#define IMU_MOVING_THRESHOLD 0.1f          // Threshold (in g) to consider IMU as moving

#define CT_TASK_PRIORITY 8
#define CART_TRACKING_INTERVAL_MS 5000     // 5 seconds

// 2.2. PAYMENT PARAMETERS
#define AUTHORIZED_UID {0x1A, 0x83, 0x26, 0x03, 0xBC}
#define AUTHORIZED_UID_LEN 5

// 3. PIN DEFINITIONS
#define USING_DEVKIT 0  // 0 = Custom PCB, 1 = ESP Devkit

// ***** DO NOT TOUCH BELOW THIS LINE UNLESS YOU KNOW WHAT YOU ARE DOING *****
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

// interface includes
#include "interfaces/barcode.h"
#include "interfaces/ble_barcode_nimble.h"
#include "interfaces/cart_tracking.h"
#include "interfaces/imu.h"
#include "interfaces/item_rfid.h"
#include "interfaces/loadcells.h"
#include "interfaces/mfrc522.h"
#include "interfaces/proximity_sensor.h"

#if USING_DEVKIT == 0   // === Custom PCB ===
// === I2C: Proximity Sensor, IMU, ===
#define SCL_PIN GPIO_NUM_9
#define SDA_PIN GPIO_NUM_8

#define PROXIMITY_INT_PIN GPIO_NUM_6

// === SPI: Payment ===
#define MOSI_PIN GPIO_NUM_11
#define SCK_PIN  GPIO_NUM_12
#define MISO_PIN GPIO_NUM_13
#define PAYMENT_CS_PIN   GPIO_NUM_10
#define PAYMENT_RST_PIN  GPIO_NUM_14


// === Pseudo SPI: Load Cells ===
#define TOP_LOAD_DATA_PIN GPIO_NUM_38
#define TOP_LOAD_CLK_PIN GPIO_NUM_39
#define BOTTOM_LOAD_DATA_PIN GPIO_NUM_40
#define BOTTOM_LOAD_CLK_PIN GPIO_NUM_41


// === UART: Barcode scanner, Item RFID, Customer RFID ===
#define BARCODE_UART_PORT UART_NUM_0
#define BARCODE_TX_PIN GPIO_NUM_43
#define BARCODE_RX_PIN GPIO_NUM_44

#define ITEM_RFID_UART_PORT UART_NUM_1
#define ITEM_RFID_TX_PIN GPIO_NUM_48
#define ITEM_RFID_RX_PIN GPIO_NUM_16

#define CART_TRACKING_UART_PORT UART_NUM_2
#define CART_TRACKING_TX_PIN GPIO_NUM_17
#define CART_TRACKING_RX_PIN GPIO_NUM_18

// === Other ===
#define BUTTON_PIN GPIO_NUM_5
#define DEBUG_LED_PIN GPIO_NUM_21
#endif

#if USING_DEVKIT == 1
// === I2C: Proximity Sensor, IMU, ===
#define SCL_PIN GPIO_NUM_9
#define SDA_PIN GPIO_NUM_8

#define PROXIMITY_INT_PIN GPIO_NUM_6

// === SPI: Payment ===
#define MOSI_PIN GPIO_NUM_11
#define SCK_PIN  GPIO_NUM_12
#define MISO_PIN GPIO_NUM_13
#define PAYMENT_CS_PIN   GPIO_NUM_10
#define PAYMENT_RST_PIN  GPIO_NUM_14


// === Pseudo SPI: Load Cells === 
#define TOP_LOAD_DATA_PIN GPIO_NUM_38
#define TOP_LOAD_CLK_PIN GPIO_NUM_39
#define BOTTOM_LOAD_DATA_PIN GPIO_NUM_40
#define BOTTOM_LOAD_CLK_PIN GPIO_NUM_41


// === UART: Barcode scanner, Item RFID, Customer RFID ===
#define BARCODE_UART_PORT UART_NUM_1
#define BARCODE_TX_PIN GPIO_NUM_1
#define BARCODE_RX_PIN GPIO_NUM_2

#define ITEM_RFID_UART_PORT UART_NUM_2
#define ITEM_RFID_TX_PIN GPIO_NUM_48
#define ITEM_RFID_RX_PIN GPIO_NUM_16

#define CART_TRACKING_UART_PORT UART_NUM_0
#define CART_TRACKING_TX_PIN GPIO_NUM_17
#define CART_TRACKING_RX_PIN GPIO_NUM_18

// === Other ===
#define BUTTON_PIN GPIO_NUM_37
#define DEBUG_LED_PIN GPIO_NUM_21
#endif

