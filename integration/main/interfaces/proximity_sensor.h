#ifndef PROX_H
#define PROX_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// APDS-9960 I2C address
#define APDS9960_I2C_ADDR       0x39

// APDS-9960 register addresses
#define APDS9960_ENABLE         0x80
#define APDS9960_ATIME          0x81
#define APDS9960_WTIME          0x83
#define APDS9960_AILTL          0x84
#define APDS9960_AILTH          0x85
#define APDS9960_AIHTL          0x86
#define APDS9960_AIHTH          0x87
#define APDS9960_PILT           0x89
#define APDS9960_PIHT           0x8B
#define APDS9960_PERS           0x8C
#define APDS9960_CONFIG1        0x8D
#define APDS9960_PPULSE         0x8E
#define APDS9960_CONTROL        0x8F
#define APDS9960_CONFIG2        0x90
#define APDS9960_ID             0x92
#define APDS9960_STATUS         0x93
#define APDS9960_CDATAL         0x94
#define APDS9960_CDATAH         0x95
#define APDS9960_RDATAL         0x96
#define APDS9960_RDATAH         0x97
#define APDS9960_GDATAL         0x98
#define APDS9960_GDATAH         0x99
#define APDS9960_BDATAL         0x9A
#define APDS9960_BDATAH         0x9B
#define APDS9960_PDATA          0x9C
#define APDS9960_POFFSET_UR     0x9D
#define APDS9960_POFFSET_DL     0x9E
#define APDS9960_CONFIG3        0x9F
#define APDS9960_GPENTH         0xA0
#define APDS9960_GEXTH          0xA1
#define APDS9960_GCONF1         0xA2
#define APDS9960_GCONF2         0xA3
#define APDS9960_GOFFSET_U      0xA4
#define APDS9960_GOFFSET_D      0xA5
#define APDS9960_GPULSE         0xA6
#define APDS9960_GOFFSET_L      0xA7
#define APDS9960_GOFFSET_R      0xA9
#define APDS9960_GCONF3         0xAA
#define APDS9960_GCONF4         0xAB
#define APDS9960_GFLVL          0xAE
#define APDS9960_GSTATUS        0xAF
#define APDS9960_IFORCE         0xE4
#define APDS9960_PICLEAR        0xE5
#define APDS9960_CICLEAR        0xE6
#define APDS9960_AICLEAR        0xE7
#define APDS9960_GFIFO_U        0xFC
#define APDS9960_GFIFO_D        0xFD
#define APDS9960_GFIFO_L        0xFE
#define APDS9960_GFIFO_R        0xFF

// ENABLE register bits
#define APDS9960_PON            0x01
#define APDS9960_AEN            0x02
#define APDS9960_PEN            0x04
#define APDS9960_WEN            0x08
#define APDS9960_AIEN           0x10
#define APDS9960_PIEN           0x20
#define APDS9960_GEN            0x40

// Acceptable device IDs
#define APDS9960_ID_1           0xAB
#define APDS9960_ID_2           0x9C

// Proximity gain values
#define APDS9960_PGAIN_1X       0x00
#define APDS9960_PGAIN_2X       0x04
#define APDS9960_PGAIN_4X       0x08
#define APDS9960_PGAIN_8X       0x0C

// ALS gain values
#define APDS9960_AGAIN_1X       0x00
#define APDS9960_AGAIN_4X       0x01
#define APDS9960_AGAIN_16X      0x02
#define APDS9960_AGAIN_64X      0x03

// LED drive strength
#define APDS9960_LED_DRIVE_100MA  0x00
#define APDS9960_LED_DRIVE_50MA   0x40
#define APDS9960_LED_DRIVE_25MA   0x80
#define APDS9960_LED_DRIVE_12_5MA 0xC0

// LED boost
#define APDS9960_LED_BOOST_100    0x00
#define APDS9960_LED_BOOST_150    0x10
#define APDS9960_LED_BOOST_200    0x20
#define APDS9960_LED_BOOST_300    0x30

// Pulse length
#define APDS9960_PULSE_LEN_4US    0x00
#define APDS9960_PULSE_LEN_8US    0x40
#define APDS9960_PULSE_LEN_16US   0x80
#define APDS9960_PULSE_LEN_32US   0xC0

// Opaque handle for proximity sensor instance
typedef struct ProximitySensor {
    i2c_master_dev_handle_t dev_handle;
    uint8_t int_pin;
    uint8_t threshold;
    bool verbose;
    bool connected;
} ProximitySensor;

// Function declarations

/**
 * @brief Create proximity sensor instance
 */
ProximitySensor* proximity_sensor_create(uint8_t int_pin, uint8_t threshold, bool verbose);

/**
 * @brief Destroy proximity sensor instance
 */
void proximity_sensor_destroy(ProximitySensor* sensor);

/**
 * @brief Initialize proximity sensor on I2C bus
 */
bool proximity_sensor_begin(ProximitySensor* sensor, i2c_master_bus_handle_t bus_handle);

/**
 * @brief Check if sensor is connected and operational
 */
bool proximity_sensor_is_connected(const ProximitySensor* sensor);

/**
 * @brief Read proximity sensor value
 */
uint8_t proximity_sensor_read(ProximitySensor* sensor);

/**
 * @brief Clear proximity sensor interrupt
 */
void proximity_sensor_clear_interrupt(ProximitySensor* sensor);

/**
 * @brief Enable proximity sensor interrupt
 */
void proximity_sensor_enable_interrupt(ProximitySensor* sensor);

/**
 * @brief Disable proximity sensor interrupt
 */
void proximity_sensor_disable_interrupt(ProximitySensor* sensor);

/**
 * @brief Set proximity sensor gain
 */
bool proximity_sensor_set_gain(ProximitySensor* sensor, uint8_t gain);

/**
 * @brief Set proximity sensor LED drive strength
 */
bool proximity_sensor_set_led_drive(ProximitySensor* sensor, uint8_t drive);

/**
 * @brief Configure proximity sensor pulse length and count
 */
bool proximity_sensor_set_pulse(ProximitySensor* sensor, uint8_t pulse_length, uint8_t pulse_count);

#endif