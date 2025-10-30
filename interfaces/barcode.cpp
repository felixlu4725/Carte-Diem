#include "barcode.h"

const uint8_t BarcodeScanner::manual_mode_cmd[9] = {0x7E,0x00,0x08,0x01,0x00,0x00,0xD5,0xAB,0xCD};
const uint8_t BarcodeScanner::continuous_mode_cmd[9] = {0x7E,0x00,0x08,0x01,0x00,0x00,0xD6,0xAB,0xCD};
const uint8_t BarcodeScanner::trigger_cmd[9] = {0x7E,0x00,0x08,0x01,0x00,0x02,0x01,0xAB,0xCD};

BarcodeScanner::BarcodeScanner(uint8_t rxPin, uint8_t txPin, bool verbose)
    : ScannerSerial(1), RX_PIN(rxPin), TX_PIN(txPin), VERBOSE(verbose) {}

void BarcodeScanner::begin() {
    ScannerSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);

    if(VERBOSE)
      Serial.println("[Barcode Scanner] ✓ Barcode scanner ready...");
    
    setManualMode();
}

bool BarcodeScanner::loop() {
    return handleData();
}

bool BarcodeScanner::handleData() {
    if (ScannerSerial.available()) {
        String data = "";
        unsigned long start = millis();
        while (millis() - start < 20) {
            if (ScannerSerial.available()) {
                uint8_t b = ScannerSerial.read();
                if (b < 0x10) data += "0";
                data += String(b, HEX) + " ";
                start = millis();
            }
        }
        data.trim();

        if (data != "02 00 00 01 00 33 31") {
            Serial.println(data);
            if (continuousMode) {
                setManualMode();
                continuousMode = false;
                return true;
            }
        }
    }
    return false;
}

void BarcodeScanner::setManualMode() {
  ScannerSerial.write(manual_mode_cmd, sizeof(manual_mode_cmd));

  if(VERBOSE)
    Serial.println("[Barcode Scanner] Manual mode ON");
}

void BarcodeScanner::setContinuousMode() {   
  continuousMode = true;
  ScannerSerial.write(continuous_mode_cmd, sizeof(continuous_mode_cmd));

  if(VERBOSE)
    Serial.println("[Barcode Scanner] Continuous mode ON");
}

void BarcodeScanner::triggerScan() {
  ScannerSerial.write(trigger_cmd, sizeof(trigger_cmd));

  if(VERBOSE)
    Serial.println("[Barcode Scanner] Triggered scan");
}
