# Carte Diem - Smart Shopping Cart 
### Abraham Vega, Ava Chang, Felix Lu, Jin Wook Shin, Natalie Do, Tina Ma
{abevega, avachang, felixlu, jinws, donatali, tinma}@umich.edu

EECS 473 F25
<p align="center">
  <img src="https://github.com/user-attachments/assets/bc05971d-ae6c-4cf4-bc1e-f0703657ef8f" alt="Expo Poster" width=100%>
</p>
Carte Diem is a add-on module that integrates smart features to regular carts. This device eliminates the need for checkout lines while maintaining anti-theft features. It’s modularity allows stores to easily implement this technology into the existing ecosystems.

### README Directories
- [PCB](PCB/README.md) — schematics, layout, and power architecture  
- [CAD](CAD/README.md) — mechanical enclosures and mounting hardware
- [cart](cart/README.md) - UI interfacing
- [Integration](integration/README.md) — ESP32 ↔ Raspberry Pi integration and BLE communication


## Motivation

Despite the widespread adoption of self-checkout, grocery stores still experience congestion, long wait times, and loss-prevention challenges. Existing smart cart solutions address these issues but come at a cost of **$5,000–$10,000 per cart**, making large-scale deployment unrealistic for most retailers.

Carte Diem explores a different design space:
- Retrofit instead of full cart replacement  
- Sensor fusion instead of single-modality detection  
- Real-time embedded verification instead of post-hoc auditing  

The goal is to eliminate checkout lines entirely while maintaining store-side trust, scalability, and cost efficiency.

---

## System Overview
<img width="2156" height="810" alt="image" src="https://github.com/user-attachments/assets/0e1dd9b7-a1c1-4b7c-a96f-a4c96f1b2ce2" />
<img width="1598" height="1188" alt="image" src="https://github.com/user-attachments/assets/6df3e5d9-ef62-4fb4-8332-7658c5c7a2cd" />

Carte Diem consists of two tightly coupled subsystems:

### On-Cart Embedded System
- **Custom ESP32-S3 PCB** running FreeRTOS
- Interfaces with all sensors and peripherals
- Performs real-time sensing, verification, and event generation
- Communicates with the Raspberry Pi via BLE

### On-Cart Compute + Backend
- **Raspberry Pi 4** provides UI, networking, and payment flow
- Connects to store and Carte Diem servers over WiFi
- Handles inventory queries, checkout confirmation, and analytics upload

---

## Key Features

### Item Detection & Verification
- **Barcode Scanning**
  - Primary item identification method
  - Button- or proximity-triggered
  - Fast and reliable for most items

- **RFID Item Verification**
  - Detects tagged items (e.g., alcohol, high-value goods)
  - Automatically cross-checks cart contents
  - Flags unscanned additions in real time

- **Weight-Based Verification**
  - Four load cells under the cart basket
  - Cross-checks expected vs. measured weight
  - Detects unscanned add/remove events
  - Freezes checkout when inconsistencies are detected

- **Produce Scale**
  - Dedicated top-mounted scale
  - Computes price dynamically based on weight

### Checkout & Security
- **Checkout & Payment**
  - Simulated RFID tap-to-pay
  - QR-code payment flow (Square API)
  - Digital receipt generation

- **GPS-Based Security**
  - Store-defined cart boundaries
  - Alarm triggered if cart exits allowed area

### Analytics & Store Insights
- **Cart Tracking**
  - RFID-based indoor tracking (no computer vision)
  - Generates cart paths and heatmaps
  - Aggregates multi-session shopping behavior

---

## Hardware Architecture

- Custom **ESP32-S3 PCB**
  - UART: barcode scanner, RFID modules
  - SPI: payment RFID
  - I²C: IMU, proximity sensor
  - ADC + GPIO: load cells
  - BLE: ESP32 ↔ Raspberry Pi
- Raspberry Pi 4 with touchscreen display
- Dedicated **12 V LiFePO₄ battery**
- Multi-rail power system (12 V, 9 V, 5 V, 3.3 V, 1.8 V)
- 3D-printed enclosures designed for durability and weather resistance

The system is designed to operate **15+ hours continuously**, matching a full day of store operation.

---

## Software Architecture

- **FreeRTOS-based firmware**
  - Periodic tasks for sensing and monitoring
  - Event-driven verification triggered by weight changes
  - ISR-to-task deferral for responsiveness and power efficiency

- **BLE Communication Protocol**
  - Structured commands for barcode scans, weight readings, RFID bursts, and cart tracking logs

- **Full-Stack Web Backend**
  - Store database and inventory management
  - Cart registration and session tracking
  - Heatmap and path visualization
  - GPS boundary configuration interface

---

## Repository Structure

```text
.
├── CAD/               # Mechanical CAD files for enclosures and mounts
├── PCB/               # Schematics, PCB layouts, and fabrication files
├── cart/              # On-cart UI (Raspberry Pi touchscreen interface)
├── integration/       # ESP32 ↔ Raspberry Pi integration and BLE logic
├── LICENSE            # GPL-3.0 license
└── README.md

```

## Results

- Fully functional prototype demonstrated at the EECS Design Expo
- Reliable real-time detection of item add/remove events using sensor fusion
- Weight-based verification successfully detected unscanned items
- RFID-based cart tracking enabled path reconstruction and heatmap generation
- End-to-end checkout flow validated, including UI, payment simulation, and receipt generation
- Power system met full-day operation targets under worst-case load conditions


## Photo Dump

<img width="1822" height="1338" alt="image" src="https://github.com/user-attachments/assets/f71f9208-e67b-4d02-afa3-90d6a1f191f6" />
<img width="992" height="964" alt="image" src="https://github.com/user-attachments/assets/41e1424a-cdfa-4568-9db4-701c8a62e445" />
<img width="938" height="1308" alt="image" src="https://github.com/user-attachments/assets/fe79f7c0-8651-49ee-8634-77d2a15d5b7c" />






