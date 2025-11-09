#ifndef LOAD_H
#define LOAD_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PRODUCE_SCALE_VALUE 222
#define WEIGHT_VERIFICATION_SCALE_VALUE 30

// Opaque handle for load cell instance
typedef struct LoadCell {
    gpio_num_t clk_pin;
    gpio_num_t data_pin;
    uint8_t gain;
    int32_t tare_offset;
    bool type; // false = produce, true = weight verification
} LoadCell;

// Function declarations

LoadCell* load_cell_create(gpio_num_t clk_pin, gpio_num_t data_pin, uint8_t gain, bool type);

void load_cell_destroy(LoadCell* lc);

void load_cell_begin(LoadCell* lc);

void load_cell_clk_low(LoadCell* lc);

void load_cell_clk_high(LoadCell* lc);

void load_cell_reset(LoadCell* lc);

void load_cell_tare(LoadCell* lc);

int32_t load_cell_read_channel(LoadCell* lc);

int32_t load_cell_read_channel_raw(LoadCell* lc);

int32_t load_cell_average_channel(LoadCell* lc);

float load_cell_display_pounds(LoadCell* lc);

void load_cell_delay_us(LoadCell* lc, uint32_t us);

#endif