#ifndef LOAD_H
#define LOAD_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PRODUCE_SCALE_VALUE 222
#define WEIGHT_VERIFICATION_SCALE_VALUE 26.2

// Opaque handle for load cell instance
typedef struct LoadCell {
    gpio_num_t clk_pin;
    gpio_num_t data_pin;
    uint8_t gain;
    float tare_offset;
    bool type; // false = produce, true = weight verification
} LoadCell;

// Function declarations

void tare_task(void* arg);

/**
 * @brief Create and initialize a load cell handle
 */
LoadCell* load_cell_create(gpio_num_t clk_pin, gpio_num_t data_pin, uint8_t gain, bool type);

/**
 * @brief Destroy and free a load cell handle
 */
void load_cell_destroy(LoadCell* lc);

/**
 * @brief Initialize load cell GPIO pins
 */
void load_cell_begin(LoadCell* lc);

/**
 * @brief Set CLK pin low
 */
void load_cell_clk_low(LoadCell* lc);

/**
 * @brief Set CLK pin high
 */
void load_cell_clk_high(LoadCell* lc);

/**
 * @brief Reset HX711 load cell module
 */
void load_cell_reset(LoadCell* lc);

/**
 * @brief Calibrate load cell to zero (tare)
 */
void load_cell_tare(LoadCell* lc);

/**
 * @brief Read raw 24-bit value from load cell
 */
int32_t load_cell_read_channel(LoadCell* lc);

/**
 * @brief Read raw 24-bit value without discarding first read
 */
int32_t load_cell_read_channel_raw(LoadCell* lc);

/**
 * @brief Average multiple load cell readings
 */
int32_t load_cell_average_channel(LoadCell* lc);

/**
 * @brief Get weight reading in pounds
 */
float load_cell_display_pounds(LoadCell* lc);

/**
 * @brief Get weight reading in ounces
 */
float load_cell_display_ounces(LoadCell* lc);

/**
 * @brief Delay for specified microseconds
 */

/**
 * @brief Delay for specified microseconds
 */
void load_cell_delay_us(LoadCell* lc, uint32_t us);

#endif