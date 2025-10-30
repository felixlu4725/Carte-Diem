#include <Wire.h>
#include <Adafruit_APDS9960.h>
#include <barcode.h>
#include <proximity_sensor.h>

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

ProximitySensor proxor(INT_PROX, PROXIMITY_THRESHOLD, VERBOSE);

// ISR
void IRAM_ATTR button_isr(void);
void IRAM_ATTR proximity_isr(void);

// ISR Flags
volatile bool button_pressed = false;
volatile bool proximity_triggered = false;

// ISR debouncing
volatile unsigned long last_button_press = 0;
const unsigned long DEBOUNCE_DELAY = 200;
volatile unsigned long prox_last_trigger_time = 0;
const unsigned long PROX_DEBOUNCE_DELAY = 1000;


void setup() {
  // Initialize USB Serial for debug output
  Serial.begin(115200);

  // Wait for serial to be ready (important for ESP32-S3)
  unsigned long start_time = millis();
  while (!Serial && (millis() - start_time) < 3000) {
    delay(10);
  }
  delay(500);  // Additional delay for stability

  // Initialize I2C lines
  TwoWire I2C = TwoWire(0);
  I2C.begin(I2C_SDA, I2C_SCL);
  I2C.setClock(100000); 

  // Initialize APDS9960 + interrupt pin
  proxor.begin(&I2C);
  pinMode(INT_PROX, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INT_PROX), proximity_isr, FALLING);

  // Initialize barcode scanner
  barcanner.begin();

  // Initialize button with interrupt
  pinMode(INT_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INT_BUTTON), button_isr, FALLING);

  Serial.print("✓ Button interrupt on GPIO ");
  Serial.println(INT_BUTTON);

  delay(500);  // Give scanner more time to boot
}

void loop() {
  // Continuously check for barcode data
  if (barcanner.loop()) { // manual mode set
    proxor.clearInterrupt();
    proxor.enableInterrupt();
  }
  
  if (button_pressed) {
    button_pressed = false;
    barcanner.triggerScan();
  }

  if (proximity_triggered) {
    proximity_triggered = false;

    barcanner.setContinuousMode();
    proxor.disableInterrupt();

    uint8_t proximity = proxor.readProximity();
    proxor.clearInterrupt();
  }

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