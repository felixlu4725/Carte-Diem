// working version Oct. 29, 5PM.
// before integrating barcode.cpp/h





#include <Wire.h>
#include <Adafruit_APDS9960.h>
#include <HardwareSerial.h>

// Pin definitions
#define TX_SCAN 38
#define RX_SCAN 39
#define INT_BUTTON 45
#define INT_PROX 47
// #define LED_PIN 2

// I2C pins for ESP32-S3
#define I2C_SDA 8
#define I2C_SCL 9

// Proximity threshold for interrupt
#define PROXIMITY_THRESHOLD 1  // Interrupt fires when proximity > this value

// Serial port for scanner
HardwareSerial ScannerSerial(1);

// APDS9960 sensor
Adafruit_APDS9960 apds;

// Flag to track if APDS9960 is connected
bool apds_connected = false;

// Protocol state machine
typedef enum {
  RX_WAIT_HEADER = 0,
  RX_WAIT_LEN_HI,
  RX_WAIT_LEN_LO,
  RX_READ_PAYLOAD
} rx_state_t;

// State variables
static volatile rx_state_t rx_state = RX_WAIT_HEADER;
static uint16_t rx_len = 0;
static uint16_t rx_pos = 0;

#define BARCODE_MAX 256
static uint8_t barcode_buf[BARCODE_MAX];

volatile bool continuous_mode = false;  // Start in manual mode
volatile bool proximity_triggered = false;
volatile bool button_pressed = false;
volatile bool mode_changed = false;
volatile bool prox_int_timeout = false;

// Button debouncing
volatile unsigned long last_button_press = 0;
const unsigned long DEBOUNCE_DELAY = 200;

volatile unsigned long prox_last_trigger_time = 0;
const unsigned long PROX_DEBOUNCE_DELAY = 1000; // 1 second in milliseconds

// Statistics
unsigned long scan_count = 0;
unsigned long proximity_trigger_count = 0;
unsigned long button_press_count = 0;

// Function prototypes
void send_barcode_mode_continuous(void);
void send_barcode_mode_manual(void);
void send_barcode_trigger(void);
void IRAM_ATTR button_isr(void);
void IRAM_ATTR proximity_isr(void);

void setup() {
  // Initialize USB Serial for debug output
  Serial.begin(115200);

  // Wait for serial to be ready (important for ESP32-S3)
  unsigned long start_time = millis();
  while (!Serial && (millis() - start_time) < 3000) {
    delay(10);
  }
  delay(500);  // Additional delay for stability

  Serial.println("\n╔═══════════════════════════════════════╗");
  Serial.println("║  ESP32-S3 Barcode Scanner System     ║");
  Serial.println("║  with APDS9960 Hardware Interrupt    ║");
  Serial.println("╚═══════════════════════════════════════╝");

  // Initialize I2C
  Serial.print("Initializing I2C (SDA: ");
  Serial.print(I2C_SDA);
  Serial.print(", SCL: ");
  Serial.print(I2C_SCL);
  Serial.println(")...");
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);  // Set I2C to 100kHz for stability
  delay(100);             // Give I2C time to initialize

  // Initialize APDS9960 interrupt pin BEFORE trying to communicate
  pinMode(INT_PROX, INPUT_PULLUP);

  // Initialize APDS9960
  Serial.print("Initializing APDS9960 sensor...");

  // Try to initialize APDS9960 with retries
  int retry_count = 0;
  const int max_retries = 3;

  while (!apds_connected && retry_count < max_retries) {
    if (apds.begin()) {
      apds_connected = true;
      Serial.println(" OK!");
      break;
    }
    retry_count++;
    Serial.print(".");
    delay(500);
  }

  if (!apds_connected) {
    Serial.println(" FAILED!");
    Serial.println("⚠ WARNING: APDS9960 not detected!");
    Serial.println("   Check connections:");
    Serial.println("   - SDA -> GPIO 8");
    Serial.println("   - SCL -> GPIO 9");
    Serial.println("   - INT -> GPIO 47");
    Serial.println("   - VIN -> 3.3V");
    Serial.println("   - GND -> GND");
    Serial.println("\n   Continuing without proximity sensor...\n");
  } else {
    // Configure APDS9960 with error checking
    Serial.println("Configuring APDS9960...");

    // Enable proximity mode
    apds.enableProximity(true);


    if (apds_connected) {
      // Add delay before setting threshold
      delay(50);

      // Set the interrupt threshold
      // Using try-catch pattern with verification
      apds.setProximityInterruptThreshold(0, PROXIMITY_THRESHOLD);  // <-- swapped range
      Serial.print("✓ Proximity interrupt when value < ");
      Serial.println(PROXIMITY_THRESHOLD);

      // Add delay before enabling interrupt
      delay(50);

      // Enable proximity interrupt
      apds.enableProximityInterrupt();
      Serial.println("✓ Proximity interrupt enabled");

      // Clear any pending interrupts
      apds.clearInterrupt();

      // Attach interrupt to ESP32 pin AFTER configuration
      delay(50);
      attachInterrupt(digitalPinToInterrupt(INT_PROX), proximity_isr, FALLING);
      Serial.print("✓ Interrupt attached to GPIO ");
      Serial.println(INT_PROX);
    }
  }

  // Initialize scanner serial port
  Serial.print("Initializing scanner UART...");
  ScannerSerial.begin(9600, SERIAL_8N1, RX_SCAN, TX_SCAN);
  delay(100);  // Give UART time to initialize
  Serial.println(" OK!");

  // Initialize LED
  // pinMode(LED_PIN, OUTPUT);
  // digitalWrite(LED_PIN, LOW);
  // Serial.println("✓ LED initialized");

  // Initialize button with interrupt
  pinMode(INT_BUTTON, INPUT_PULLUP);
  delay(50);
  attachInterrupt(digitalPinToInterrupt(INT_BUTTON), button_isr, FALLING);
  Serial.print("✓ Button interrupt on GPIO ");
  Serial.println(INT_BUTTON);

  // Initialize parser state
  // Set scanner to manual mode by default
  delay(500);  // Give scanner more time to boot
  send_barcode_mode_manual();

  Serial.println("\n╔═══════════════════════════════════════╗");
  Serial.println("║           SYSTEM READY                ║");
  Serial.println("╠═══════════════════════════════════════╣");
  Serial.println("║ Controls:                             ║");
  Serial.println("║ • Button (GPIO 45): Toggle mode       ║");
  if (apds_connected) {
    Serial.println("║ • Proximity interrupt: Trigger scan   ║");
    Serial.println("║   (in manual mode only)               ║");
  } else {
    Serial.println("║ • Proximity: NOT AVAILABLE            ║");
  }
  Serial.println("╚═══════════════════════════════════════╝\n");
}

void loop() {
  // Process incoming bytes from scanner
  if(ScannerSerial.available()){
    String hexBuffer = "";
    unsigned long startTime = millis();
    
    // Wait for all data to arrive (timeout after no new data for 50ms)
    while (millis() - startTime < 20) {
      if (ScannerSerial.available()) {
        uint8_t b = ScannerSerial.read();
        
        if (b < 0x10) hexBuffer += "0";
        hexBuffer += String(b, HEX);
        hexBuffer += " ";
        
        startTime = millis();  // Reset timer when new data arrives
      }
    }
    
    // Remove trailing space and check if it's the confirmation packet
    hexBuffer.trim();
    if (hexBuffer != "02 00 00 01 00 33 31") {
      // Print all at once (only if not confirmation packet)
      Serial.println(hexBuffer);
      if(continuous_mode){
        send_barcode_mode_manual();
        continuous_mode = false;

        // apds.setProximityInterruptThreshold(0, PROXIMITY_THRESHOLD);
        apds.clearInterrupt();
        apds.enableProximityInterrupt();
      }
    }
  }

  // Handle mode change notification
  if (button_pressed) {
    button_pressed = false;
    send_barcode_trigger();
    button_press_count++;
  }

  // Handle proximity interrupt trigger (only if APDS9960 is connected)
  if (proximity_triggered && apds_connected) {
    proximity_triggered = false;

    apds.disableProximityInterrupt();

    continuous_mode = true;
    send_barcode_mode_continuous();

    // Read the proximity value with error checking
    uint8_t proximity = 0;

    // Use a try-catch style approach
    if (apds_connected) {
      proximity = apds.readProximity();

      Serial.print("🎯 [PROXIMITY] Interrupt! Value: ");
      Serial.print(proximity);
      Serial.println(" → Triggering scan...");

      // Clear the APDS9960 interrupt
      apds.clearInterrupt();

      proximity_trigger_count++;
    }
  }

  // Add a small delay to prevent watchdog issues
  delay(1);
}

void IRAM_ATTR button_isr(void) {
  unsigned long current_time = millis();

  // Debounce
  if (current_time - last_button_press > DEBOUNCE_DELAY) {
    last_button_press = current_time;
    button_pressed = true;
  }
}

void IRAM_ATTR proximity_isr(void) {
  // ONLY set flags - NO I2C calls, NO object access
  unsigned long current_time = millis();
  
  if (current_time - prox_last_trigger_time >= PROX_DEBOUNCE_DELAY) {
    proximity_triggered = true;
    prox_last_trigger_time = current_time;
  }
}

void send_barcode_mode_continuous(void) {
  uint8_t cmd[] = { 0x7E, 0x00, 0x08, 0x01, 0x00, 0x00, 0xD6, 0xAB, 0xCD };
  ScannerSerial.write(cmd, sizeof(cmd));
  Serial.println("\n🔄 [MODE] CONTINUOUS scanning enabled");
  // Serial.println("   Scanner will scan automatically");
  // Serial.println("   (Proximity interrupt disabled)\n");
}

void send_barcode_mode_manual(void) {
  uint8_t manual_cmd[] = { 0x7E, 0x00, 0x08, 0x01, 0x00, 0x00, 0xD5, 0xAB, 0xCD };
  ScannerSerial.write(manual_cmd, sizeof(manual_cmd));
  Serial.println("\n👆 [MODE] MANUAL scanning enabled");
  // if (apds_connected) {
  //   Serial.println("   Proximity interrupt active");
  //   Serial.println("   Move object close to trigger scan\n");
  // } else {
  //   Serial.println("   (Proximity sensor not available)\n");
  // }
}

void send_barcode_trigger(void) {
  // Send a manual trigger command to the scanner
  // Note: This command may need adjustment for your specific scanner model
  uint8_t trigger_cmd[] = { 0x7E, 0x00, 0x08, 0x01, 0x00, 0x02, 0x01, 0xAB, 0xCD };
  ScannerSerial.write(trigger_cmd, sizeof(trigger_cmd));
}
