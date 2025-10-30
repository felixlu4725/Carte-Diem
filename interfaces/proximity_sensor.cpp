#include <proximity_sensor.h>

ProximitySensor::ProximitySensor(uint8_t intPin, uint8_t threshold, bool verbose)
    : INT_PIN(intPin), THRESHOLD(threshold), VERBOSE(verbose) {
    apds = Adafruit_APDS9960();  
}

void ProximitySensor::begin(TwoWire *theWire) {
    if (apds.begin(10, APDS9960_AGAIN_4X, APDS9960_ADDRESS, theWire)) {
        connected = true;

        // Enable proximity mode
        apds.enableProximity(true);

        // Set the interrupt threshold
        apds.setProximityInterruptThreshold(0, THRESHOLD);

        if(VERBOSE){
            Serial.print("[Proximity sensor] interrupt when value < ");
            Serial.println(THRESHOLD);
        }

        // Enable proximity interrupt
        apds.enableProximityInterrupt();
        apds.clearInterrupt();

        if(VERBOSE)
            Serial.println("[Proximity sensor] ✓ Proximity sensor ready...");

    } else {
        connected = false;
    }
}

uint8_t ProximitySensor::readProximity() {
    uint8_t proximity = 0;
    proximity = apds.readProximity();

    if(VERBOSE){
        Serial.print("[Proximity sensor] Proximity value: ");
        Serial.println(proximity);
    }

    return proximity;
}
void ProximitySensor::clearInterrupt() {
    apds.clearInterrupt();
}
void ProximitySensor::enableInterrupt() {
    apds.enableProximityInterrupt();
}
void ProximitySensor::disableInterrupt() {
    apds.disableProximityInterrupt();
}