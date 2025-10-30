#ifndef PROX_H
#define PROX_H

#include <Wire.h>
#include <Adafruit_APDS9960.h>

class ProximitySensor {
public:
    ProximitySensor(uint8_t intPin, uint8_t threshold, bool verbose = false);
    void begin(TwoWire *theWire);
    void loop();

    bool isConnected() const { return connected; }
    uint8_t readProximity();
    void clearInterrupt();
    void enableInterrupt();
    void disableInterrupt();

private:
    Adafruit_APDS9960 apds;

    uint8_t INT_PIN;
    uint8_t THRESHOLD;
    bool VERBOSE;
    bool connected = false;
};

#endif