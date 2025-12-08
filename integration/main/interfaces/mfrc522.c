#include "mfrc522.h"
#include "esp_log.h"
#include "freertos/task.h"

#define TAG "MFRC522"

// Common MFRC522 registers and commands
#define PCD_IDLE        0x00
#define PCD_TRANSCEIVE  0x0C
#define PCD_AUTHENT     0x0E
#define PCD_SOFTRESET   0x0F
#define PCD_CALCCRC     0x03

#define CommandReg      0x01
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define BitFramingReg   0x0D
#define ComIrqReg       0x04
#define ErrorReg        0x06
#define ModeReg         0x11
#define TxModeReg       0x12
#define RxModeReg       0x13
#define TxControlReg    0x14
#define TModeReg        0x2A
#define TPrescalerReg   0x2B
#define TReloadRegH     0x2C
#define TReloadRegL     0x2D
#define TxASKReg        0x15 

#define PICC_REQIDL     0x26
#define PICC_ANTICOLL   0x93

// --- SPI read/write helpers (fixed shifting macros) ------------------------

/**
 * @brief Write value to MFRC522 register via SPI
 */
static void write_reg(mfrc522_t *dev, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { MFRC522_WRITE_CMD(reg), val };
    uint8_t rx[2] = {0};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = buf,
        .rx_buffer = rx
    };
    gpio_set_level(dev->cs_pin, 0);
    esp_err_t err = spi_device_transmit(dev->spi, &t);
    gpio_set_level(dev->cs_pin, 1);

    if (err != ESP_OK)
        ESP_LOGE(TAG, "write_reg failed: reg=0x%02X val=0x%02X err=%d", reg, val, err);
    ESP_LOGD(TAG, "write_reg: reg=0x%02X val=0x%02X cmd=0x%02X rx[0]=0x%02X rx[1]=0x%02X",
             reg, val, buf[0], rx[0], rx[1]);
}

/**
 * @brief Read value from MFRC522 register via SPI
 */
static uint8_t read_reg(mfrc522_t *dev, uint8_t reg) {
    uint8_t tx[2] = { MFRC522_READ_CMD(reg), 0x00 };
    uint8_t rx[2] = {0};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx
    };
    gpio_set_level(dev->cs_pin, 0);
    esp_err_t err = spi_device_transmit(dev->spi, &t);
    gpio_set_level(dev->cs_pin, 1);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read_reg failed: reg=0x%02X err=%d", reg, err);
        return 0;
    }

    ESP_LOGD(TAG, "read_reg: reg=0x%02X tx[0]=0x%02X rx[0]=0x%02X rx[1]=0x%02X",
             reg, tx[0], rx[0], rx[1]);

    return rx[1];
}

// --- Bit manipulation helpers ---------------------------------------------

/**
 * @brief Set bit mask in MFRC522 register
 */
static void set_bit_mask(mfrc522_t *dev, uint8_t reg, uint8_t mask) {
    uint8_t tmp = read_reg(dev, reg);
    write_reg(dev, reg, tmp | mask);
}

/**
 * @brief Clear bit mask in MFRC522 register
 */
static void clear_bit_mask(mfrc522_t *dev, uint8_t reg, uint8_t mask) {
    uint8_t tmp = read_reg(dev, reg);
    write_reg(dev, reg, tmp & (~mask));
}

// --- Core functions -------------------------------------------------------

/**
 * @brief Perform soft reset of MFRC522 chip
 */
void mfrc522_reset(mfrc522_t *dev) {
    write_reg(dev, CommandReg, PCD_SOFTRESET);
    vTaskDelay(pdMS_TO_TICKS(50));
}

/**
 * @brief Initialize MFRC522 RFID reader over SPI
 */
esp_err_t mfrc522_init(mfrc522_t *dev, spi_host_device_t host,
                       gpio_num_t miso, gpio_num_t mosi, gpio_num_t sck,
                       gpio_num_t cs, gpio_num_t rst)
{
    ESP_LOGI(TAG, "Initializing SPI with MISO=%d MOSI=%d SCK=%d CS=%d RST=%d", miso, mosi, sck, cs, rst);
    spi_bus_config_t buscfg = {
        .miso_io_num = miso,
        .mosi_io_num = mosi,
        .sclk_io_num = sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };
    esp_err_t ret = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus initialization failed: %d (0x%x)", ret, ret);
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "  → SPI host already initialized. Check if SPI2_HOST is used elsewhere.");
        }
        return ret;
    }
    ESP_LOGI(TAG, "SPI bus initialized successfully");

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 4 * 1000 * 1000, // 4 MHz
        .mode = 0,
        .spics_io_num = -1, 
        .queue_size = 1,
    };
    ret = spi_bus_add_device(host, &devcfg, &dev->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device addition failed: %d (0x%x)", ret, ret);
        return ret;
    }
    ESP_LOGI(TAG, "SPI device added successfully with 4 MHz clock");

    dev->cs_pin  = cs;
    dev->rst_pin = rst;
    gpio_set_direction(cs, GPIO_MODE_OUTPUT);
    gpio_set_level(cs, 1);
    gpio_set_direction(rst, GPIO_MODE_OUTPUT);
    gpio_set_level(rst, 1);
    ESP_LOGI(TAG, "CS and RST pins configured and set to HIGH");
    vTaskDelay(pdMS_TO_TICKS(10));

    // Perform hard reset sequence
    ESP_LOGI(TAG, "Performing hard reset: RST LOW → HIGH");
    gpio_set_level(rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rst, 1);
    vTaskDelay(pdMS_TO_TICKS(100)); 

    // Test basic SPI communication before soft reset
    ESP_LOGI(TAG, "Testing basic SPI read after hard reset...");
    for (int i = 0; i < 3; i++) {
        uint8_t test_val = read_reg(dev, 0x37); // VersionReg
        ESP_LOGI(TAG, "  Test read %d: VersionReg = 0x%02X", i+1, test_val);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // --- Arduino-style PCD_Init sequence ---
    ESP_LOGI(TAG, "Performing soft reset...");
    write_reg(dev, CommandReg, PCD_SOFTRESET);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Wait for PowerDown bit to clear
    uint8_t cmd_reg;
    int timeout = 100;
    while (timeout-- > 0) {
        cmd_reg = read_reg(dev, CommandReg);
        if (!(cmd_reg & (1 << 4))) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGI(TAG, "CommandReg after reset = 0x%02X", cmd_reg);

    // Configure recommended defaults
    write_reg(dev, TModeReg,       0x80);
    write_reg(dev, TPrescalerReg,  0xA9);
    write_reg(dev, TReloadRegH,    0x03);
    write_reg(dev, TReloadRegL,    0xE8);
    write_reg(dev, TxASKReg,       0x40);
    write_reg(dev, ModeReg,        0x3D);

    // Enable antenna driver pins (TX1, TX2)
    write_reg(dev, TxControlReg, read_reg(dev, TxControlReg) | 0x03);
    ESP_LOGI(TAG, "Antenna enabled, TxControlReg=0x%02X", read_reg(dev, TxControlReg));


    ESP_LOGI(TAG, "Reading chip version...");
    uint8_t version = read_reg(dev, 0x37); // VersionReg
    ESP_LOGI(TAG, "Chip version: 0x%02X", version);

    if (version == 0x00) {
        ESP_LOGE(TAG, "ERROR: Chip version is 0x00 - SPI communication failure!");
        ESP_LOGE(TAG, "Check: 1) Power supply, 2) Wiring, 3) SPI pins, 4) CS/RST pins");
        return ESP_FAIL;
    }

    // Accept known MFRC522 versions: 0x92, 0x91 (official), 0x3F (compatible/clone variant)
    if (version != 0x92 && version != 0x91) {
        ESP_LOGW(TAG, "WARNING: Unexpected chip version 0x%02X (expected 0x91, 0x92)", version);
    } else {
        ESP_LOGI(TAG, "Chip version 0x%02X is supported", version);
    }

    return ESP_OK;
}

/**
 * @brief Send PICC_REQIDL command to detect cards
 */
static esp_err_t picc_request(mfrc522_t *dev, uint8_t *tag_type) {
    write_reg(dev, BitFramingReg, 0x07);
    write_reg(dev, CommandReg, PCD_IDLE);
    write_reg(dev, FIFOLevelReg, 0x80);

    write_reg(dev, FIFODataReg, PICC_REQIDL);
    write_reg(dev, CommandReg, PCD_TRANSCEIVE);
    set_bit_mask(dev, BitFramingReg, 0x80);

    vTaskDelay(pdMS_TO_TICKS(5));
    clear_bit_mask(dev, BitFramingReg, 0x80);

    uint8_t irq = read_reg(dev, ComIrqReg);
    // ESP_LOGI(TAG, "REQA IRQ=0x%02X", irq);

    if (!(irq & 0x30)) {
        ESP_LOGW(TAG, "No REQA response (ComIrqReg=0x%02X)", irq);
        return ESP_FAIL;
    }

    tag_type[0] = read_reg(dev, FIFODataReg);
    tag_type[1] = read_reg(dev, FIFODataReg);
    // ESP_LOGI(TAG, "ATQA response: 0x%02X 0x%02X", tag_type[0], tag_type[1]);
    return ESP_OK;
}

/**
 * @brief Perform anticollision to get card UID
 */
static esp_err_t picc_anticoll(mfrc522_t *dev, uint8_t *uid, uint8_t *uid_len)
{
    write_reg(dev, BitFramingReg, 0x00);
    write_reg(dev, CommandReg, PCD_IDLE);
    write_reg(dev, FIFOLevelReg, 0x80); // Flush FIFO

    // Load anticollision command
    write_reg(dev, FIFODataReg, PICC_ANTICOLL);
    write_reg(dev, FIFODataReg, 0x20);

    write_reg(dev, CommandReg, PCD_TRANSCEIVE);
    set_bit_mask(dev, BitFramingReg, 0x80); // StartSend

    // Wait for IdleIRq (bit 4) or RxIRq (bit 5)
    uint8_t irq = 0, err = 0;
    int timeout = 100;
    do {
        vTaskDelay(pdMS_TO_TICKS(1));
        irq = read_reg(dev, ComIrqReg);
        err = read_reg(dev, ErrorReg);
    } while (!(irq & 0x30) && --timeout > 0);

    clear_bit_mask(dev, BitFramingReg, 0x80); // Stop sending

    if (err & 0x13) {
        ESP_LOGW(TAG, "Anticollision error: ErrorReg=0x%02X", err);
        return ESP_FAIL;
    }
    if (timeout == 0) {
        ESP_LOGW(TAG, "Anticollision timeout (no full response)");
        return ESP_FAIL;
    }

    // Wait until at least 5 bytes appear in FIFO
    uint8_t fifo_level = 0;
    for (int i = 0; i < 10; i++) {
        fifo_level = read_reg(dev, FIFOLevelReg);
        if (fifo_level >= 5) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    *uid_len = fifo_level;
    // ESP_LOGI(TAG, "UID length reported: %d", *uid_len);

    if (*uid_len < 5 || *uid_len > MFRC522_MAX_UID_LEN) {
        // ESP_LOGW(TAG, "Invalid UID length (fifo_level=%d)", fifo_level);
        return ESP_FAIL;
    }

    for (uint8_t i = 0; i < *uid_len; i++)
        uid[i] = read_reg(dev, FIFODataReg);

    // Optional BCC validation
    uint8_t bcc = 0;
    for (int i = 0; i < 4; i++) bcc ^= uid[i];
    if (bcc != uid[4])
        ESP_LOGW(TAG, "BCC mismatch: expected 0x%02X, got 0x%02X", bcc, uid[4]);

    return ESP_OK;
}



/**
 * @brief Read UID from RFID card in proximity
 */
esp_err_t mfrc522_read_uid(mfrc522_t *dev, uint8_t *uid, uint8_t *uid_len) {
    uint8_t tag_type[2];
    if (picc_request(dev, tag_type) != ESP_OK) {
        // ESP_LOGD(TAG, "mfrc522_read_uid: picc_request failed");
        return ESP_FAIL;
    }
    if (picc_anticoll(dev, uid, uid_len) != ESP_OK) {
        // ESP_LOGD(TAG, "mfrc522_read_uid: picc_anticoll failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "UID read successfully: %d bytes", *uid_len);
    return ESP_OK;
}
