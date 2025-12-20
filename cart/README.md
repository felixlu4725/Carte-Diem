# Cart Interface

This repository contains the backend logic for a cart interface system. The system consists of two primary components: a **Master Server** (Central Inventory/Management) and a **Cart Operation Node** (The physical cart logic).

## Project Structure

* **`master.py`**: The central Flask server that manages the product database, inventory levels, and serves as the source of truth for all carts in the store.
* **`cart_ops.py`**: The client-side logic running on the physical shopping cart (e.g., Raspberry Pi). It handles hardware integration (BLE sensors, GPS), cart session management, and payment processing.

---

## Component 1: Master Server (`master.py`)

The Master Server acts as the centralized database and API for the store.
<img width="1416" height="1012" alt="image" src="https://github.com/user-attachments/assets/08480100-aabd-4342-8d59-ebf591e3e203" />

### Features
* **Product Database**: Stores UPCs, prices, weights, descriptions, and RFID tags via SQLite (`products.db`).
* **Inventory Management**: Updates stock levels in real-time as carts add items.
* **Data Import**: Supports importing product catalogs via CSV.
* **Frontend Hosting**: Capable of spawning React/Electron UI processes (currently commented out).

### API Endpoints
| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `POST` | `/lookup_product` | Returns product details + verification requirements for a specific UPC. |
| `POST` | `/lookup_rfid` | Returns product details associated with a list of RFID tags. |
| `POST` | `/update_stock` | Decrements or increments inventory quantity. |
| `POST` | `/add_complete_product` | Adds or updates a product (including images, aisle location, etc.). |
| `POST` | `/import_csv` | Bulk import products from CSV text. |
| `POST` | `/execute_sql` | Direct SQL execution for database maintenance. |

### Setup & Run
1.  Install dependencies:
    ```bash
    pip install flask flask_cors flask_socketio
    ```
2.  Run the server:
    ```bash
    python master.py
    ```
    * *Default Host:* `0.0.0.0`
    * *Default Port:* `80`

---

## Component 2: Cart Operations (`cart_ops.py`)

This script controls the physical shopping cart hardware and logic. It is designed to run in two modes: as a background daemon (handling BLE/GPS) and as a CLI utility called by a UI (likely Electron) to perform specific actions.
<img width="1740" height="720" alt="image" src="https://github.com/user-attachments/assets/77455a2d-1542-4e5b-8812-2b72e5f2eb26" />


### Features
* **BLE Integration**: Connects to a peripheral device (ESP32/Arduino) to receive:
    * Barcode scans (UPC).
    * Load cell data (Produce weight).
    * RFID tags (Anti-theft/Verification).
    * IMU data (Activity detection).
* **Security & Geofencing**: Uses GPS (NMEA) to detect if the cart leaves a defined boundary polygon (`boundary.json`). Triggers audio alarms if theft is detected.
* **Smart Weight Verification**: Compares the expected weight of items in the cart DB against real-time scale measurements to prevent theft.
* **Payments**: Distinct integration with **Square API** for generating checkout links and polling payment status.
* **Receipts**: Sends email receipts via SMTP.

### Hardware Dependencies
* **BLE Device**: Expects a device named `"Carte_Diem"` (Address configurable in code).
* **GPS Module**: Expects NMEA stream on `/dev/serial0` at 9600 baud.
* **Audio**: Uses `mpg123` (Linux) or `afplay` (macOS) for alarms.
* **Monitor**: Uses `ddcutil` to adjust screen brightness based on activity.

### Setup & Configuration
1.  Install dependencies:
    ```bash
    pip install requests squareup bleak aiohttp pyserial timezonefinder pytz pynmea2
    ```
    ```
    cd shopping-ui
    npm run install
    ```
2.  **Database**: Ensure `cart.db` and `setting.db` are initialized.
3.  **API Keys**:
    * Update `SQUARE_ACCESS_TOKEN` in `cart_ops.py` with your Square Sandbox/Production token.
    * Update SMTP credentials in `send_receipt` function.

### Usage

1. Start the server with:
```bash
python master.py
```
2. Run the interface by going into shopping-ui:
```bash
cd shopping-ui
npm run start
```
In a new terminal run going into shopping-ui:
```bash
cd shopping-ui
npm run electron
```
## Database Schemas

This document outlines the SQLite database structures used in the Smart Shopping Cart system.

## 1. Master Database (`products.db`)
**Location:** Master Server  
**Managed by:** `master.py`  
**Purpose:** Stores the central inventory, product details, images, and categorization. It serves as the source of truth for all carts.

### `Products`
The core inventory table.
```sql
CREATE TABLE IF NOT EXISTS "Products" (
    upc TEXT PRIMARY KEY,
    brand TEXT,
    description TEXT,
    qty INTEGER,
    price REAL,
    weight REAL,
    produce INTEGER
);

CREATE TABLE AisleLocations (
    upc TEXT PRIMARY KEY,
    aisle TEXT,
    section TEXT,
    side TEXT,
    description TEXT,
    FOREIGN KEY (upc) REFERENCES Products(upc) ON DELETE CASCADE
);

CREATE TABLE Categories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT UNIQUE
);

CREATE TABLE ProductCategories (
    upc TEXT,
    category_id INTEGER,
    PRIMARY KEY (upc, category_id),
    FOREIGN KEY (upc) REFERENCES Products(upc) ON DELETE CASCADE,
    FOREIGN KEY (category_id) REFERENCES Categories(id) ON DELETE CASCADE
);

CREATE TABLE Images (
    upc TEXT,
    size TEXT,
    url TEXT,
    PRIMARY KEY (upc, size),
    FOREIGN KEY (upc) REFERENCES Products(upc) ON DELETE CASCADE
);

CREATE TABLE ProductRFIDs (
    upc TEXT NOT NULL,
    rfid_id TEXT,
    PRIMARY KEY (upc, rfid_id),
    FOREIGN KEY (upc) REFERENCES Products(upc) ON DELETE CASCADE
);

CREATE TABLE restricted_items (
    upc TEXT PRIMARY KEY,
    description TEXT
);
```

## 2. Cart Database (`cart.db`)
**Location:** Client  
**Managed by:** `cart_ops.py`  
**Purpose:** Stores the items in the cart with qty, weight, and prices for frontend.

### `Cart`
The cart table.
```sql
CREATE TABLE cart (
        upc TEXT PRIMARY KEY,
        description TEXT NOT NULL,
        price REAL NOT NULL,
        qty INTEGER NOT NULL,
        subtotal REAL NOT NULL,
        weight REAL NOT NULL, 
        requires_verification INTEGER DEFAULT 0,
        resolved INTEGER DEFAULT 0,
        produce INTEGER DEFAULT 0);
```

## 2. Cart Database (`setting.db`)
**Location:** Client  
**Managed by:** `cart_ops.py`  
**Purpose:** Stores the address and port to connect to the stores and Carte Diem servers to reach endpoints.

### `Settings`
The settings table.
```sql
CREATE TABLE setting (
    id INTEGER PRIMARY KEY,
    master_ip TEXT,
    master_port TEXT,
    company_ip TEXT,
    company_port TEXT,
    setup_complete INTEGER DEFAULT 0,
    hardware_id TEXT);
```
