# Carte Diem — PCB

This directory contains the **custom ESP32-S3-based PCB** designed for the Carte Diem smart shopping cart system.  
The board serves as the central embedded controller, interfacing with all on-cart sensors, peripherals, and the Raspberry Pi.

---

## Overview

The PCB is responsible for:
- Real-time sensor acquisition
- Item verification and cart state monitoring
- Power distribution and regulation
- BLE communication with the Raspberry Pi

It was designed to support **all-day operation**, modular peripherals, and reliable performance in a noisy retail environment.

---

## Key Features

- **ESP32-S3 MCU**
  - Runs FreeRTOS
  - Handles sensing, control, and BLE communication

- **Peripheral Interfaces**
  - UART: barcode scanner, item RFID, cart-tracking RFID
  - SPI: payment RFID module
  - I²C: IMU, proximity sensor
  - GPIO + ADC: load cells (via HX711)

- **Power Architecture**
  - 12 V input (external battery)
  - On-board buck and LDO regulation
  - Supports 12 V, 9 V, 5 V, 3.3 V, and 1.8 V rails

- **Connectivity**
  - BLE for bidirectional communication with Raspberry Pi
  - Dedicated connectors for modular sensor attachment

---

## Design Notes

- EMI mitigation was required due to proximity of RFID modules and load cells  
- Power paths were designed conservatively to support peak current draw  
- The board went through multiple revisions to resolve rail shorting and voltage tolerance issues  

---

# Schematic

## Top level
<img width="1690" height="1164" alt="image" src="https://github.com/user-attachments/assets/4e64fee1-0b7b-4faf-97c8-b5374a5cf20d" />

## Power Distribution
<img width="1406" height="1012" alt="image" src="https://github.com/user-attachments/assets/5f9b87c8-65d6-46be-9d68-4d278105c485" />

## Integrated IMU
<img width="1438" height="920" alt="image" src="https://github.com/user-attachments/assets/bc37799c-04d9-4257-b06d-57b09b55f8de" />

# Routing
<img width="1236" height="1500" alt="image" src="https://github.com/user-attachments/assets/7bd44487-73fc-40cb-bd26-254487c2384b" />


# Manufractured PCB
<img width="1530" height="1170" alt="image" src="https://github.com/user-attachments/assets/24c9dbca-9fd7-4139-91af-da6a9b2d392d" />
