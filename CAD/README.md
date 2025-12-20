# Carte Diem — Mechanical CAD

This directory contains the **mechanical CAD designs** for the Carte Diem smart shopping cart system.  
The models focus on durability, ease of installation, and compatibility with standard grocery carts, while protecting electronics from everyday wear and environmental exposure.

---

## Overview

The CAD designs support:
- Secure mounting of electronics to existing shopping carts
- Protection of PCBs, batteries, and wiring
- Easy access for maintenance and installation
- Robustness against frequent handling and vibration
- 3D printing in batches for assembly

All parts were designed to be **retrofit-compatible**, requiring minimal modification to the cart itself.

<img width="1460" height="964" alt="image" src="https://github.com/user-attachments/assets/f77da356-f715-4a55-9a9e-c7496f03a4ab" />

---

## Key Components

- **Main Module Enclosure**
  - Houses the ESP32 PCB, power electronics, and wiring
  - Designed to fit within the cart’s child seat area
  - Provides mechanical protection and limited weather resistance

- **Barcode Scanner Enclosure**
  - Handheld and docked operation
  - Integrated trigger button support
  - Designed for repeated user interaction

- **RFID Antenna Mounts**
  - Side-mounted enclosures for item verification antennas
  - Dedicated mount for cart-tracking RFID module
  - Optimized placement to balance read range and interference

- **Load Cell Mounts**
  - 3D-printed fixtures to level and stabilize load cells
  - Designed to compensate for uneven cart geometry
  - Supports both bottom basket scale and produce scale

---

## Design Considerations

- **Durability**  
  Parts were reinforced to withstand daily cart use, impacts, and vibration.

- **EMI Awareness**  
  Mechanical layouts were coordinated with shielding strategies to reduce interference between RFID modules and load cells.

- **Serviceability**  
  Screwed assemblies allow store staff to access electronics without damaging parts.

- **Manufacturability**  
  Designs were optimized for rapid prototyping via FDM 3D printing, with future mass-manufacturing in mind.

---

## Fabrication Notes

- All parts were 3D printed in PETG

- Printed using a Bambu Lab A1 Mini with AMS Lite

- PETG was selected for improved toughness, temperature resistance, and impact durability

- Brass threaded inserts were used where repeated assembly was required

- Designs assume common fasteners to simplify installation and maintenance

## Status

These CAD models represent a validated prototype revision used in the final integrated system demonstrated at expo.
They are suitable for further iteration but are not production-certified.
