#include <Wire.h>
#include <Adafruit_APDS9960.h>
#include <barcode.h>

#define VERBOSE false

// Barcode scanner pins
#define TX_BARCODE 38
#define RX_BARCODE 39
#define INT_BUTTON 45

BarcodeScanner barcanner(RX_BARCODE, TX_BARCODE, VERBOSE);

// I2C pins for ESP32-S3
#define I2C_SDA 8
#define I2C_SCL 9

// Proximity sensor defines
#define INT_PROX 47
#define PROXIMITY_THRESHOLD 1

// APDS9960 sensor
Adafruit_APDS9960 apds;

// Flag to track if APDS9960 is connected
bool apds_connected = false;


volatile bool proximity_triggered = false;
volatile bool button_pressed = false;
volatile bool prox_int_timeout = false;

// Button debouncing
volatile unsigned long last_button_press = 0;
const unsigned long DEBOUNCE_DELAY = 200;

volatile unsigned long prox_last_trigger_time = 0;
const unsigned long PROX_DEBOUNCE_DELAY = 1000;

// Statistics
unsigned long scan_count = 0;
unsigned long proximity_trigger_count = 0;
unsigned long button_press_count = 0;

// ISR
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

  // Initialize barcode scanner
  barcanner.begin();

  // Initialize button with interrupt
  pinMode(INT_BUTTON, INPUT_PULLUP);
  delay(50);
  attachInterrupt(digitalPinToInterrupt(INT_BUTTON), button_isr, FALLING);
  Serial.print("✓ Button interrupt on GPIO ");
  Serial.println(INT_BUTTON);

  // Initialize parser state
  delay(500);  // Give scanner more time to boot
}

void loop() {
  // Continuously check for barcode data
  if (barcanner.loop()) { // manual mode set
    apds.clearInterrupt(); 
    apds.enableProximityInterrupt(); 
  }
  

  // Handle mode change notification
  if (button_pressed) {
    button_pressed = false;
    barcanner.triggerScan();
    button_press_count++;
  }

  // Handle proximity interrupt trigger (only if APDS9960 is connected)
  if (proximity_triggered && apds_connected) {
    proximity_triggered = false;

    apds.disableProximityInterrupt();

    barcanner.setContinuousMode();

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

  if (current_time - last_button_press > DEBOUNCE_DELAY) {
    last_button_press = current_time;
    button_pressed = true;
  }
}

void IRAM_ATTR proximity_isr(void) {
  unsigned long current_time = millis();
  
  if (current_time - prox_last_trigger_time >= PROX_DEBOUNCE_DELAY) {
    proximity_triggered = true;
    prox_last_trigger_time = current_time;
  }
}