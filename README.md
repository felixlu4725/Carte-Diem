# Carte Diem Project

## Build and Configuration

### ESP-IDF Version
This project is built using **ESP-IDF v5.5.1**. Ensure you have the correct version installed and configured before building.

### Building the Project
```bash
idf.py build
idf.py flash
idf.py monitor
```

## Configuration Settings

All configurable settings are defined in `main/cartediem_defs.h`. Modify these values to customize the behavior of the system.

### 1. Enable Functionalities
Set to `1` to enable or `0` to disable:

```c
#define ENABLE_LED_DEBUG_STARTUP 1
#define ENABLE_ITEM_VERIFICATION 0
#define ENABLE_CART_TRACKING 0
#define ENABLE_WEIGHT_MONITORING 1
```

### 2. Adjustable Parameters

```c
#define WEIGHT_CHANGE_THRESHOLD_LBS 0.5f    // Weight change threshold to trigger Item Verification
#define CART_TRACKING_INTERVAL_MS 10000     // 10 seconds
#define IMU_MOVING_THRESHOLD 0.1f           // Threshold (in g) to consider IMU as moving
#define IMU_IDLE_TIME_MINUTES 5             // 5 minutes
#define PROXIMITY_THRESHOLD 30              // Proximity sensor threshold value
```

### 3. Pin Definitions

```c
#define USING_DEVKIT 0  // 0 = Custom PCB, 1 = ESP Devkit
```

Select your hardware platform by setting `USING_DEVKIT` accordingly.

## BLE Communication

Refer to `Carte Diem BLE Commands.pdf` for detailed documentation on all available BLE commands and protocol specifications.

### Quick Testing with BLE Apps
For rapid testing and debugging of BLE functionality, you can use:
- **nRF Connect** (iOS/Android) - Provides a user-friendly interface for scanning, connecting, and sending BLE commands
- Other compatible BLE testing applications

Simply connect to the Carte Diem device and use the commands documented in the BLE Commands PDF.
