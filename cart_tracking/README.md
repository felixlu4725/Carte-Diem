# CarteDiem Backend Server

## Overview

This backend server is designed to work with CarteDiem's main MCU, the STM32 to collect RFID data in real-time. It processes the data streams sent from your `sendToServer()` function and provides comprehensive analytics for cart/customer tracking and store optimization.

### Backend Side
- Receives data at `POST /cart/data`
- Processes CartPathEntry arrays
- Provides real-time analytics via REST endpoints

## Quick Start

### 1. Installation
pip install -r requirements.txt

### 2. Start the Server
python app.py
The server will start on `http://localhost:5100`
Store servers will start on `http://localhost:5100 + Number of Stores Created`

## API Endpoints
LIST HERE WHICH ENDPOINTS FOR WHICH CATEGORY

### Core Data Collection

### Real-time Analytics  

### Customer Insights


## Database Schema

The system uses SQLite with the following key tables:

1. **Cart_#** When a session starts a cart RFID table to created and added to until a customer pays and the session ends
3. 


## Data Flow

1. **STM32 Cart** collects RFID readings using your `makeEntry()` function
2. **Batch Collection** accumulates entries until `MAX_ENTRIES` reached
3. **Data Transmission** sends batch via `sendToServer()` to backend
4. **Real-time Processing** analyzes data and updates customer session
5. **Analytics Generation** ADDs to total heat map?
6. **API Access** provides data via REST endpoints for dashboards/frontend/website

## Stretch Goals

### Analytics
- **Abandonment Detection**: Identify potential cart abandonment
- **Cross-selling Opportunities**: Detect complementary product clusters (STRETCH)
- **Traffic Update**: Use heatmap to generate concentrated areas map
