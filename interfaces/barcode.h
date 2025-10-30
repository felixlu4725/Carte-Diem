#ifndef BARCODE_H
#define BARCODE_H

#include <HardwareSerial.h>

class BarcodeScanner {
public:
    BarcodeScanner(uint8_t rxPin, uint8_t txPin, bool verbose = false);
    void begin();
    bool loop();

    void setManualMode();
    void setContinuousMode();
    void triggerScan();

    bool isContinuous() const { return continuousMode; }
    void setContinuous(bool val) { continuousMode = val; }

private:
    bool handleData();

    HardwareSerial ScannerSerial;
    uint8_t RX_PIN, TX_PIN;
    bool continuousMode = false;
    bool VERBOSE = false;

    // Command buffers (defined in barcode.cpp)
    static const uint8_t manual_mode_cmd[9];
    static const uint8_t continuous_mode_cmd[9];
    static const uint8_t trigger_cmd[9];
};

#endif