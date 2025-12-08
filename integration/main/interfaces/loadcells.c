#include "loadcells.h"
#include <math.h>
#include <time.h>
#include "esp_rom_sys.h"

void tare_task(void* arg) {
    LoadCell* lc = (LoadCell*)arg;
    printf("Taring...\n");
    load_cell_tare(lc);
    printf("Tare done. Offset = %.2f\n", lc->tare_offset);

    vTaskDelete(NULL); // Done
}

/**
 * @brief Create and initialize a load cell handle
 */
LoadCell* load_cell_create(gpio_num_t clk_pin, gpio_num_t data_pin, uint8_t gain, bool type) {
	LoadCell* cell = (LoadCell*) malloc(sizeof(LoadCell));
	if (cell == NULL) {
		return NULL;
	}
	cell->clk_pin = clk_pin;
	cell->data_pin = data_pin;
	cell->gain = gain;
	cell->type = type;

	return cell;
}

/**
 * @brief Destroy and free a load cell handle
 */
void load_cell_destroy(LoadCell* lc) {
	if (lc == NULL) return;
	free(lc);
}

/**
 * @brief Initialize load cell GPIO pins
 */
void load_cell_begin(LoadCell* lc) {
    // Configure CLK pin as output
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << lc->clk_pin),
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    gpio_config(&io_conf);

    // Configure DATA pin as input with pull-up
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << lc->data_pin);
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    // Reset HX711
    load_cell_reset(lc);
}

/**
 * @brief Set CLK pin low
 */
void load_cell_clk_low(LoadCell* lc) {
	gpio_set_level(lc->clk_pin, 0);
}

/**
 * @brief Set CLK pin high
 */
void load_cell_clk_high(LoadCell* lc) {
	gpio_set_level(lc->clk_pin, 1);
}

/**
 * @brief Reset HX711 load cell module
 */
void load_cell_reset(LoadCell* lc) {
	load_cell_clk_high(lc);
    vTaskDelay(pdMS_TO_TICKS(1));
    load_cell_clk_low(lc);
    vTaskDelay(pdMS_TO_TICKS(800)); // was 800
}

/**
 * @brief Calibrate load cell to zero (tare)
 */
void load_cell_tare(LoadCell* lc) {
	long acc = 0;
	for (int i = 0; i < 3; i++) {
		acc += load_cell_average_channel(lc);
	}
	lc->tare_offset = (float)acc/3.0;
}

/**
 * @brief Read raw 24-bit value from load cell
 */
int32_t load_cell_read_channel(LoadCell* lc) {
	load_cell_read_channel_raw(lc); // discard to set up gain
	return (load_cell_read_channel_raw(lc));
}

/**
 * @brief Read raw 24-bit value without discarding first read
 */
int32_t load_cell_read_channel_raw(LoadCell* lc) {
	while (gpio_get_level(lc->data_pin) == 1) vTaskDelay(pdMS_TO_TICKS(1));
	load_cell_clk_low(lc);
    uint32_t val = 0;
    for (int i = 0; i < 24; i++) { // read 24 bits
    	load_cell_clk_high(lc);
    	load_cell_delay_us(lc, 1);
    	val = (val << 1) | gpio_get_level(lc->data_pin);
    	load_cell_clk_low(lc);
    	load_cell_delay_us(lc, 1);
    }

    // set gain for next run
    load_cell_clk_high(lc);
  	load_cell_clk_low(lc);

    if (val & 0x800000) {
    	val |= 0xFF000000;
    }

    return (int32_t)val;
}

/**
 * @brief Average multiple load cell readings
 */
int32_t load_cell_average_channel(LoadCell* lc){
	int32_t buf[5];
	for (uint8_t i = 0; i < 5; i++) {
		buf [i] = load_cell_read_channel(lc);
    	vTaskDelay(pdMS_TO_TICKS(5));
	}

	// insertion sort to isolate outliers
	for (uint8_t i = 1; i < 5; i++) {
		int32_t key = buf[i];
		int8_t j = i-1;
		while (j >= 0 && buf[j] > key) {
			buf[j+1] = buf[j];
			j--;
		}
		buf[j+1] = key;
	}

	// ignore two smallest and two largest values (potential outliers)
	int32_t sum = 0;
	uint8_t start = 1;
	uint8_t end = 4;

	for (uint8_t i = start; i < end; i++) {
		sum += buf[i];
	}

	return (sum / (end-start));
}

/**
 * @brief Get weight reading in pounds
 */
float load_cell_display_pounds(LoadCell* lc) {
	int32_t raw = load_cell_average_channel(lc);
	float net = raw - lc->tare_offset;

	float scale = lc->type ? WEIGHT_VERIFICATION_SCALE_VALUE : PRODUCE_SCALE_VALUE; 
	float grams = net/scale;

	if (fabs(grams) < 3.0f) {
		grams = 0;
	}
	else {
		grams = fabs(grams);
		grams = roundf(grams*10) / 10.0f;
	}

	float pounds = grams * 0.00220462;

	return pounds;
}

/**
 * @brief Get weight reading in ounces
 */
float load_cell_display_ounces(LoadCell* lc) {
	int32_t raw = load_cell_average_channel(lc);
	float net = raw - lc->tare_offset;

	float scale = lc->type ? WEIGHT_VERIFICATION_SCALE_VALUE : PRODUCE_SCALE_VALUE;
	float grams = net/scale;

	if (fabs(grams) < 3.0f) {
		grams = 0;
	}
	else {
		grams = fabs(grams);
		grams = roundf(grams*10) / 10.0f;
	}
	float ounces = grams * 0.035274;

	return ounces;
}

/**
 * @brief Delay for specified microseconds
 */
void load_cell_delay_us(LoadCell* lc, uint32_t us) {
    // struct timespec start, now;
    // clock_gettime(CLOCK_MONOTONIC, &start);

    // uint64_t start_us = start.tv_sec * 1000000ULL + start.tv_nsec / 1000;
    // uint64_t target = start_us + us;

    // do {
    //     clock_gettime(CLOCK_MONOTONIC, &now);
    // } while ((now.tv_sec * 1000000ULL + now.tv_nsec / 1000) < target);
	esp_rom_delay_us(us);
}