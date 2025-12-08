import uuid
import time
import requests
import sqlite3
import threading
import random
import json
import sys
import json
from square import Square
from square.environment import SquareEnvironment
from uuid import uuid4
import traceback
import asyncio
from bleak import BleakClient, BleakScanner
import aiohttp
import re
import subprocess
import serial
from timezonefinder import TimezoneFinder
import pytz
import pynmea2
from datetime import datetime
import os
import smtplib
from email.message import EmailMessage

MASTER_URL = None
COMPANY_URL = None

BLE_DEVICE_ADDRESS = "9c:13:9e:a8:5d:f5"

client = None

UPC_CHARACTERISTIC_UUID = "e36b6c81-1d7e-4589-b289-794fb676b14f"
PRODUCE_WEIGHT_CHARACTERISTIC_UUID = "9f50361c-8ce0-4655-9903-577ca0c7db68"
ITEM_VERIFICATION_CHARACTERISTIC_UUID = "c7a12053-7add-4a60-bd43-af8f0d171dce"
CT_RFID_CHARACTERISTIC_UUID = "d5eabd06-05a6-4b8d-bb15-8393c3a703de"
PAYMENT_CHARACTERISTIC_UUID = "45ef2927-fd6a-4ba2-ab82-f3f5f27b7967"
TRANSFER_UUID = "81de7ab2-7bb5-4a08-91ad-73165d9d2bb0"
MISC_UUID = "b8ce8946-c4d4-486a-91fe-9fea2a670262"

OFFSET_TOLERANCE = 12

# Database helper functions
def get_setting_conn():
    conn = sqlite3.connect(SETTING_DB)
    conn.row_factory = sqlite3.Row
    return conn

def get_cart_conn():
    conn = sqlite3.connect(CART_DB)
    conn.row_factory = sqlite3.Row
    return conn

def get_cart_items():
    try:
        conn = get_cart_conn()
        rows = conn.execute("SELECT * FROM cart").fetchall()
        conn.close()

        items = [dict(r) for r in rows]
        
        return {"status": "success", "items": items}
    except Exception as e:
        return {"status": "error", "message": str(e)}

def set_brightness(level):
    if not (0 <= level <= 100):
        raise ValueError("Brightness must be between 0 and 100")

    try:
        subprocess.run(
            ["ddcutil", "setvcp", "10", str(level)],
            check=True
        )
        print(f"External monitor brightness set to {level}%", file=sys.stderr)
        return {"status": "success", "level": level}

    except FileNotFoundError:
        msg = "ddcutil not found. Please install it."
        print(msg, file=sys.stderr)
        return {"status": "error", "message": msg}

    except subprocess.CalledProcessError:
        msg = "Failed to set brightness. Ensure DDC/CI is enabled and you have permission."
        print(msg, file=sys.stderr)
        return {"status": "error", "message": msg}


def fetch_and_save_boundary(url, filename="boundary.json"):
    try:
        response = requests.get(url, timeout=15)
        response.raise_for_status()
        data = response.json()

        if data.get("type") != "Polygon":
            raise ValueError("Received JSON is not a Polygon")

        boundaries = data.get("boundaries", [])
        if not boundaries:
            raise ValueError("No boundaries found in response")

        with open(filename, "w") as f:
            json.dump(data, f, indent=2)

        print(f"Polygon successfully fetched from {url} and written to {filename}", file=sys.stderr)

    except requests.Timeout:
        print(f"Error fetching polygon from {url}: Request timed out", file=sys.stderr)
    except requests.RequestException as e:
        print(f"Error fetching polygon from {url}: {e}", file=sys.stderr)
    except Exception as e:
        print(f"Error processing polygon data: {e}", file=sys.stderr)
        
PORT = '/dev/serial0'
BAUD = 9600
tf = TimezoneFinder()
latest_coords = {"lat": None, "lon": None, "time": None, "tz": None}
lock = threading.Lock()

def parse_gps():
    """Continuously parse GPS NMEA data and check if inside boundary polygon."""
    
    with open("boundary.json", "r") as f:
        data = json.load(f)
    BOUNDARY = data["boundaries"]

    def point_on_segment(p, a, b):
        (x, y) = p
        (x1, y1) = a
        (x2, y2) = b

        if abs((y - y1) * (x2 - x1) - (x - x1) * (y2 - y1)) > 1e-10:
            return False

        return (
            min(x1, x2) <= x <= max(x1, x2)
            and min(y1, y2) <= y <= max(y1, y2)
        )

    def point_in_polygon(point, polygon):
        x, y = point
        inside = False

        j = len(polygon) - 1
        for i in range(len(polygon)):
            xi, yi = polygon[i]
            xj, yj = polygon[j]

            # Point on boundary
            if point_on_segment(point, (xi, yi), (xj, yj)):
                return True

            intersect = (
                (yi > y) != (yj > y)
                and x < (xj - xi) * (y - yi) / (yj - yi + 1e-12) + xi
            )

            if intersect:
                inside = not inside

            j = i

        return inside

    ser = serial.Serial(PORT, BAUD, timeout=1)
    print(f"Listening on {PORT} at {BAUD} baud...\n", file=sys.stderr)

    last_alarm = 0
    ALARM_COOLDOWN = 10
    prev_inside = None

    while True:
        # print("Waiting for GPS data...", file=sys.stderr)
        line = ser.readline().decode("ascii", errors="replace").strip()

        if line.startswith("$GPRMC"):
            try:
                msg = pynmea2.parse(line)

                if msg.status == "A":
                    lat, lon = msg.latitude, msg.longitude

                    # GeoJSON uses (lon, lat)
                    point = (lon, lat)

                    inside = point_in_polygon(point, BOUNDARY)

                    utc_dt = datetime.combine(msg.datestamp, msg.timestamp)
                    tz_name = tf.timezone_at(lat=lat, lng=lon) or "UTC"
                    local_tz = pytz.timezone(tz_name)
                    local_time = utc_dt.replace(tzinfo=pytz.utc).astimezone(local_tz)

                    with lock:
                        latest_coords.update({
                            "lat": lat,
                            "lon": lon,
                            "time": local_time,
                            "tz": tz_name,
                            "inside_boundary": inside
                        })

                    print(f"📍 Latitude:  {lat:.6f}", file=sys.stderr)
                    print(f"📍 Longitude: {lon:.6f}", file=sys.stderr)
                    print(f"🕒 Local time: {local_time.strftime('%Y-%m-%d %H:%M:%S')} ({tz_name})", file=sys.stderr)
                    print(f"🧭 Inside Boundary: {inside}", file=sys.stderr)
                    print("-" * 50, file=sys.stderr)
                    
                    if not inside and time.time() - last_alarm > ALARM_COOLDOWN:
                        subprocess.Popen([
                            "mpg123", "-a", "hw:0,0", "alarm.mp3"
                        ])
                        last_alarm = time.time()

                    # Mac test
                    # if not inside and time.time() - last_alarm > ALARM_COOLDOWN:
                    #     subprocess.Popen(["afplay", "alarm.mp3"])
                    #     last_alarm = time.time()

                    if prev_inside is None:
                        cmd = "INDOOR_MODE_ON" if inside else "OUTDOOR_MODE_ON"
                        asyncio.run(send_ble_command_async(cmd))
                        print("Sent INDOOR_MODE_ON", file=sys.stderr) if inside else print("Sent OUTDOOR_MODE_ON", file=sys.stderr)
                    elif inside != prev_inside:
                        cmd = "INDOOR_MODE_ON" if inside else "OUTDOOR_MODE_ON"
                        asyncio.run(send_ble_command_async(cmd))
                        print("Sent INDOOR_MODE_ON", file=sys.stderr) if inside else print("Sent OUTDOOR_MODE_ON", file=sys.stderr)

                    prev_inside = inside
                    
            except Exception as e:
                print(f"GPS parse error: {e}", file=sys.stderr)

# Setup functions
def get_setup():
    conn = sqlite3.connect(SETTING_DB)
    cursor = conn.cursor()
    cursor.execute("""
        SELECT master_ip, master_port, company_ip, company_port, setup_complete
        FROM setting
        WHERE id = 1
    """)
    row = cursor.fetchone()
    conn.close()

    if not row:
        row = (None, None, None, None, 0)

    return {
        "status": "success",
        "master_ip": row[0],
        "master_port": row[1],
        "company_ip": row[2],
        "company_port": row[3],
        "setup_complete": row[4]
    }

def save_setup(master_ip, master_port, company_ip, company_port):
    conn = sqlite3.connect(SETTING_DB)
    cursor = conn.cursor()
    cursor.execute("""
        UPDATE setting
        SET master_ip = ?, master_port = ?, company_ip = ?, company_port = ?, setup_complete = 1, hardware_id = ?
        WHERE id = 1
    """, (master_ip, master_port, company_ip, company_port, BLE_DEVICE_ADDRESS))
    conn.commit()
    conn.close()

    try:
        url = f"http://{company_ip}:{company_port}/register_cart"
        payload = {"hardware_id": BLE_DEVICE_ADDRESS}
        headers = {"Content-Type": "application/json"}
        response = requests.post(url, data=json.dumps(payload), headers=headers, timeout=15)

        if response.status_code == 200:
            resp_json = response.json()
            print(f"[REGISTER_CART] Success: {resp_json}", file=sys.stderr)
        else:
            print(f"[REGISTER_CART] Failed with status {response.status_code}: {response.text}", file=sys.stderr)

    except requests.Timeout:
        print("[REGISTER_CART] Request timed out!", file=sys.stderr)
    except requests.RequestException as e:
        print(f"[REGISTER_CART] Error calling endpoint: {e}", file=sys.stderr)

    return {"status": "success"}

def get_master_url():
    conn = sqlite3.connect(SETTING_DB)
    cursor = conn.cursor()
    cursor.execute("SELECT master_ip, master_port, setup_complete FROM setting WHERE id = 1")
    row = cursor.fetchone()
    conn.close()

    if row and row[2] == 1:  # setup_complete = 1
        master_ip, master_port = row[0], row[1]
        if master_ip and master_port:
            return f"http://{master_ip}:{master_port}"
        else:
            raise ValueError("Master IP or port not set in DB")
    else:
        return None  # setup not done or setup_complete != 1
    
def get_company_url():
    conn = sqlite3.connect(SETTING_DB)
    cursor = conn.cursor()
    cursor.execute("SELECT company_ip, company_port, setup_complete FROM setting WHERE id = 1")
    row = cursor.fetchone()
    conn.close()

    if row and row[2] == 1:  # setup_complete = 1
        company_ip, company_port = row[0], row[1]
        if company_ip and company_port:
            return f"http://{company_ip}:{company_port}"
        else:
            raise ValueError("Company IP or port not set in DB")
    else:
        return None  # setup not done or setup_complete != 1
    
def notify_ui(product):
    """Send newly added product to Electron via stdout for IPC."""
    if not isinstance(product, dict):
        print("PRODUCT_ADDED_JSON:" + json.dumps({"error": "invalid product"}))
        sys.stdout.flush()
        return
    
    print("PRODUCT_ADDED_JSON:" + json.dumps(product))
    sys.stdout.flush()

async def handle_upc_notification(sender, data):
    MASTER_URL = get_master_url()
    # 1234567890123
    # upc
    upc = data.decode().strip()
    print(f"Received UPC via BLE: {upc}", file=sys.stderr)
    
    try:
        payload = {"upc": upc}
        response = requests.post(f"{MASTER_URL}/lookup_product", json=payload)
        response.raise_for_status()
        resp_json = response.json()

        product = resp_json["product"]
        
        print(f"DEBUG lookup_product({upc}) -> {product}", file=sys.stderr)
        if product:
            print(f"DEBUG types: price={product.get('price')}({type(product.get('price'))}), "
                f"weight={product.get('weight')}({type(product.get('weight'))})", file=sys.stderr)

        if not product:
            print(f"Product with UPC {upc} not found.", file=sys.stderr)
            return

        cart_item = add_to_cart(product, 1)
        notify_ui(cart_item)

    except Exception as e:
        print(f"Failed to add UPC {upc} to cart: {e}", file=sys.stderr)

async def handle_produce_weight_notification(sender, data):
    try:
        weight_str = data.decode().strip()
        print(f"Received produce weight via BLE: {weight_str} oz", file=sys.stderr)

        weight = float(weight_str)

        if weight == 0:
            # Schedule the command again without blocking the loop
            await asyncio.sleep(1)
            asyncio.create_task(send_ble_command_async("MEASURE_PROD_WEIGHT"))
            return

        produce_json = {
            "produce-weight-received": weight
        }

        # Send JSON to stdout for Electron
        print("PRODUCE_WEIGHT_JSON:" + json.dumps(produce_json))
        sys.stdout.flush()

    except ValueError:
        print(f"Failed to parse produce weight: {data}", file=sys.stderr)
    except Exception as e:
        print(f"Error handling produce weight: {e}", file=sys.stderr)

async def handle_item_verification_notification(sender, data):
    MASTER_URL = get_master_url()
    # item rfid will only send unique tags
    # 27,3,tag1,tag2,tag3
    # weight,num_tags,tag1,tag2,tag3
    try:
        decoded = data.decode().strip()
        parts = decoded.split(',')

        if len(parts) < 2:
            print(f"Invalid data format: {decoded}", file=sys.stderr)
            return
        
        weight = float(parts[0]) * 16.0
        num_tags = int(parts[1])
        tags = parts[2:]

        print(f"Received item verification via BLE:", file=sys.stderr)
        print(f"  Measured Weight: {weight} oz", file=sys.stderr)
        print(f"  Number of tags: {num_tags}", file=sys.stderr)
        print(f"  Tags: {tags}", file=sys.stderr)

        conn = get_cart_conn()
        cursor = conn.cursor()

        cursor.execute("SELECT SUM(weight) FROM cart")
        result = cursor.fetchone()
        total_cart_weight = result[0] if result and result[0] else 0.0
        conn.close()

        print(f"Real cart weight: {total_cart_weight:.2f} oz", file=sys.stderr)

        diff = abs(weight - total_cart_weight)

        if diff <= OFFSET_TOLERANCE:
            print(f"Weight verification passed (diff={diff:.2f} oz)", file=sys.stderr)
            verification_passed = True
        else:
            print(f"Weight verification failed (diff={diff:.2f} oz)", file=sys.stderr)
            verification_passed = False

        if num_tags > 0:
            try:
                response = requests.post(
                    f"{MASTER_URL}/lookup_rfid",
                    json={"tags": tags}
                )
                response.raise_for_status()
                rfid_products = response.json().get("products", [])
            except requests.RequestException as e:
                print(f"Warning: Could not lookup RFID: {e}", file=sys.stderr)
                rfid_products = []

            conn = get_cart_conn()
            cursor = conn.cursor()
            cursor.execute("SELECT upc FROM cart")
            cart_upcs = {row["upc"] for row in cursor.fetchall()}
            conn.close()

            scanned_upcs = {p["upc"] for p in rfid_products}
            unscanned_items = scanned_upcs - cart_upcs

            if unscanned_items:
                print(f"Some scanned items are not in the cart: {unscanned_items}", file=sys.stderr)
                verification_passed = False
            else:
                print("All RFID scanned items are present in the cart.", file=sys.stderr)
                verification_passed = verification_passed and True
        else:
            print("No tags scanned; skipping RFID/cart verification.", file=sys.stderr)

        verification_json = {
            "item-verification-received": {
                "status": "success" if verification_passed else "failed",
                "measured_weight": weight,
                "expected_weight": total_cart_weight,
                "weight_difference": diff,
                "tags_scanned": tags,
                "rfid_upcs": list(scanned_upcs) if num_tags > 0 else [],
                "verification_passed": verification_passed
            }
        }

        print("ITEM_VERIFICATION_JSON:" + json.dumps(verification_json), flush=True)

    except Exception as e:
        print(f"Failed to parse item verification data: {e}", file=sys.stderr)
        return {"error": str(e), "verification_passed": False}

file_receiving = False
file_buffer = None
file_expected_size = 0
file_name = None

async def upload_file_async(path, url):
    print(f"[UPLOAD] Uploading {path} to {url}...", file=sys.stderr)

    try:
        timeout = aiohttp.ClientTimeout(total=30)

        async with aiohttp.ClientSession(timeout=timeout) as session:
            with open(path, "rb") as f:
                form = aiohttp.FormData()
                form.add_field(
                    "txt_file",
                    f,
                    filename=os.path.basename(path),
                    content_type="application/octet-stream"
                )

                form.add_field(
                    "hardware_id",
                    BLE_DEVICE_ADDRESS
                )

                async with session.post(url, data=form) as resp:
                    text = await resp.text()
                    print("[UPLOAD] Status:", resp.status, file=sys.stderr)
                    print("[UPLOAD] Response:", text, file=sys.stderr)

    except Exception as e:
        print(f"[UPLOAD] Failed to upload {path}: {e}", file=sys.stderr)

file_receiving = False
file_buffer = []
file_name = "session.txt"

async def process_received_file(file_name, lines):
    print(f"[FILE] Finished receiving ({len(lines)} lines)", file=sys.stderr)

    try:
        with open(file_name, "w") as f:
            for line in lines:
                f.write(line + "\n")

        print(f"[FILE] Saved to {file_name}", file=sys.stderr)

    except Exception as e:
        print(f"[FILE] Failed to save file: {e}", file=sys.stderr)
        return

    try:
        COMPANY_URL = get_company_url()
        await upload_file_async(file_name, f"{COMPANY_URL}/cart/data")

    except Exception as e:
        print(f"[UPLOAD] Error: {e}", file=sys.stderr)

async def handle_ct_rfid_notification(sender, data):
    # 2,{tag1,rssi,t1};{tag2,rssi,t2} 
    # num_tags,{tag1,rssi,t1};{tag2,rssi,t2}
    global file_receiving, file_buffer, file_name

    try:
        text = data.decode("utf-8").strip()
    except Exception:
        return

    if text.startswith("FILE_START"):
        file_receiving = True
        file_buffer = []
        file_name = "session.txt"
        print("[FILE] Start receiving session.txt", file=sys.stderr)
        return

    if text.startswith("FILE_END") and file_receiving:
        file_receiving = False

        finished_lines = file_buffer.copy()
        finished_name = file_name

        file_buffer = []

        asyncio.create_task(process_received_file(finished_name, finished_lines))

        return

    if file_receiving:
        if text:
            file_buffer.append(text)
            print(f"[FILE] Lines received: {len(file_buffer)}", file=sys.stderr)
            await asyncio.sleep(0.002)

# test for sending file to Carte Diem
# async def test_file_upload_from_txt(file_path):
#     global file_receiving, file_buffer, file_name

#     # Simulate starting the file
#     await handle_ct_rfid_notification(None, b"FILE_START")

#     # Read the sample file line by line
#     with open(file_path, "rb") as f:
#         for line in f:
#             line = line.strip()
#             if line:
#                 await handle_ct_rfid_notification(None, line)

#     # Simulate end of file
#     await handle_ct_rfid_notification(None, b"FILE_END")
    
async def handle_payment_notification(sender, data):
    # 0 or 1
    # boolean failure or success

    raw = data.decode().strip()
    print(f"Received payment notification via BLE: {raw}", file=sys.stderr)

    try:
        if raw not in {"0", "1"}:
            print(f"Unexpected payment value: {raw}", file=sys.stderr)
            return

        payment_json = {
            "payment-received": {
                "status": "success" if raw == "1" else "failed"
            }
        }

        # Send JSON to stdout for Electron to pick up
        print("PAYMENT_JSON:" + json.dumps(payment_json), flush=True)

    except Exception as e:
        print(f"Failed to process payment notification: {e}", file=sys.stderr)

async def handle_misc_notification(sender, data):
    try:
        raw = data.decode().strip()
        print(f"Received misc notification via BLE: {raw}", file=sys.stderr)

        # Match messages of the form [COMPONENT] message
        match = re.match(r"\[(\w+)]\s*(.*)", raw)
        if match:
            component, message = match.groups()

            if component == "IMU":
                if message.upper() == "IDLE":
                    print("Brightness lowered", file=sys.stderr)
                    set_brightness(0)
                    return
                
                imu_status_json = {
                    "imu-activity-received": message.upper()
                }

                set_brightness(100)
                print("Brightness maxed", file=sys.stderr)

                print("IMU_ACTIVITY_JSON:" + json.dumps(imu_status_json), flush=True)

            else:
                other_status_json = {
                    "component": component,
                    "message": message
                }
                print("MISC_JSON:" + json.dumps(other_status_json), flush=True)

        else:
            print(f"Unhandled misc BLE message: {raw}", file=sys.stderr)

    except Exception as e:
        print(f"Failed to process misc notification: {e}", file=sys.stderr)

async def send_ble_command_async(command_char):
    if client is None or not client.is_connected:
        return {"status": "error", "message": "BLE client not connected"}
    try:
        await client.write_gatt_char(TRANSFER_UUID, bytes(command_char, "utf-8"))
        # print(f"Sent command '{command_char}'", file=sys.stderr)
        return {"status": "success", "command": command_char}
    except Exception as e:
        return {"status": "error", "message": str(e)}

async def wait_for_device(name, timeout=30):
    for _ in range(timeout):
        devices = await BleakScanner.discover()
        for d in devices:
            if d.name == name:
                return d.address
        await asyncio.sleep(1)
    return None

async def connect_and_listen(address):
    """Connects to a BLE device and subscribes to notifications."""
    global client
    async with BleakClient(address) as client:
        print("Connected to BLE device:", address, file=sys.stderr)

        # Wait for service discovery
        for service in client.services:
            print(f"Service {service.uuid}", file=sys.stderr)

        # Subscribe to notifications
        async def safe_notify(uuid, handler, label):
            if uuid in [c.uuid for s in client.services for c in s.characteristics]:
                await client.start_notify(uuid, handler)
                print(f"Subscribed to {label} ({uuid})", file=sys.stderr)

        await safe_notify(UPC_CHARACTERISTIC_UUID, handle_upc_notification, "UPC")
        await safe_notify(PRODUCE_WEIGHT_CHARACTERISTIC_UUID, handle_produce_weight_notification, "Produce Weight")
        await safe_notify(ITEM_VERIFICATION_CHARACTERISTIC_UUID, handle_item_verification_notification, "Item Verification")
        await safe_notify(CT_RFID_CHARACTERISTIC_UUID, handle_ct_rfid_notification, "CT RFID")
        await safe_notify(PAYMENT_CHARACTERISTIC_UUID, handle_payment_notification, "Payment")
        await safe_notify(MISC_UUID, handle_misc_notification, "Misc")
        
        # Wait until disconnected
        try:
            while True:
                if not client.is_connected:
                    print("⚠️ Disconnected from device.", file=sys.stderr)
                    break
                await asyncio.sleep(1)
        except asyncio.CancelledError:
            pass
        except Exception as e:
            print(f"Error in BLE loop: {e}", file=sys.stderr)
        finally:
            print("Connection closed.", file=sys.stderr)

async def main():
    DEVICE_NAME = "Carte_Diem"

    while True:
        try:
            # Keep scanning until device is found
            address = None
            attempt = 1
            while not address:
                print(f"Searching for '{DEVICE_NAME}' (attempt {attempt})...", file=sys.stderr)

                address = await wait_for_device(DEVICE_NAME, timeout=5)

                if not address:
                    print("     Not found. Retrying in 2 seconds...\n", file=sys.stderr)
                    await asyncio.sleep(2)
                    attempt += 1

            print(f"✅ Found device at {address}", file=sys.stderr)

            # Connect and stay connected
            await connect_and_listen(address)

        except Exception as e:
            print(f"⚠️ BLE connection error: {e}", file=sys.stderr)

        print("🔁 Reconnecting in 5 seconds...", file=sys.stderr)
        await asyncio.sleep(5)

# Square Payment
square_client = Square(
    token="EAAAl3tfbdieBPWf_bA5k2TY58NRssBMcrCtJG5FPamfzff-Y6Ghypvh0AyplCim",
    environment=SquareEnvironment.SANDBOX
)

SETTING_DB = "setting.db"
CART_DB = "cart.db"

client_id = f"{random.randint(0, 9999):04d}"

SQUARE_ACCESS_TOKEN = "EAAAl3tfbdieBPWf_bA5k2TY58NRssBMcrCtJG5FPamfzff-Y6Ghypvh0AyplCim"
SQUARE_API_BASE = "https://connect.squareup.com/v2"

# Track polling threads to avoid duplicates
active_order_pollers = {}
def get_order_status(order_id):
    """
    Poll payments associated with a checkout order to check if any payment is completed.
    """
    try:
        if not order_id:
            return {"status": "ERROR", "message": "No order_id provided"}

        # List recent payments (limit 100)
        payments_result = square_client.payments.list(
            sort_order="DESC",
            limit=100
        )

        # Find a payment associated with this order
        for p in payments_result:
            if p.order_id == order_id:
                status = p.status  # "COMPLETED", "APPROVED", "CANCELED"
                payment_id = p.id
                return {"status": status, "order_id": order_id, "payment_id": payment_id}

        # If no payment yet
        return {"status": "PENDING", "order_id": order_id}

    except Exception as e:
        print(f"[ERROR] Failed to get order status {order_id}: {e}")
        return {"status": "ERROR", "message": str(e)}

def poll_square_order(order_id):
    payments_api = square_client.payments

    while True:
        try:
            # Search for payments associated with the order
            body = {
                "query": {
                    "filter": {
                        "order_filter": {
                            "order_ids": [order_id]
                        }
                    }
                }
            }

            result = payments_api.search_payments(body)
            payments = result.payments

            if payments and len(payments) > 0:
                payment = payments[0]
                status = payment.status
                print(f"[DEBUG] Payment status = {status}")

                if status in ("APPROVED","COMPLETED", "CANCELED", "FAILED"):
                    return status

            time.sleep(2)

        except Exception as e:
            print(f"[ERROR] Error polling payment for order {order_id}: {e}")
            time.sleep(2)

def start_polling(order_id):
    """Start background polling thread."""
    if order_id in active_order_pollers:
        return {"status": "already_polling"}

    t = threading.Thread(    target=poll_square_order,    args=(order_id,))
    active_order_pollers[order_id] = t
    t.start()
    return {"status": "started"}

# start a new session on the cart
def start_session():
    session_id = str(uuid.uuid4())

    conn = sqlite3.connect(CART_DB)
    c = conn.cursor()
    c.execute("INSERT INTO sessions (id) VALUES (?)", (session_id,))
    conn.commit()
    conn.close()

    print(f"New shopping session created: {session_id}", file=sys.stderr)
    return {"status": "success", "session_id": session_id}

# end the cart session, clears the cart
def end_session():
    conn = get_cart_conn()
    c = conn.cursor()
    c.execute("SELECT id FROM sessions ORDER BY ROWID DESC LIMIT 1")
    row = c.fetchone()
    conn.close()
    session_id = row["id"] if row else None
    try:

        print(f"Shopping session {session_id} ended", file=sys.stderr)

        conn = get_cart_conn()
        c = conn.cursor()

        c.execute("DELETE FROM cart")
        c.execute("DELETE FROM sessions")
        conn.commit()
        conn.close()

        session_id = None

        # Return success response
        return {"status": "success", "message": "Cart session ended successfully"}
    except Exception as e:
        return {"status": "error", "message": str(e)}

# calculate the total price of the cart
def calculate_total():
    conn = get_cart_conn()
    cursor = conn.cursor()

    # Sum the subtotal column for all rows in the cart table
    cursor.execute("SELECT SUM(subtotal) FROM cart")
    total = cursor.fetchone()[0]

    conn.close()

    # where cart is empty 0.0
    return total if total is not None else 0.0

def add_to_cart(product, amount):
    MASTER_URL = get_master_url()
    # Produce: amount is the measured weight
    # Non-produce: amount is the qty
    
    if product is None:
        raise ValueError("add_to_cart called with product=None")

    # Ensure UPC is a string
    upc = str(product.get("upc", "")).strip()
    is_produce = product.get("produce") == 1

    # Normalize price and per-unit weight
    price = product.get("price")

    if is_produce:
        weight_per_unit = 1.0
    else:
        weight_per_unit = float(product.get("weight"))

    try:
        price = float(price)
    except Exception:
        raise ValueError(f"Product {upc!r} missing or invalid price")

    # Ensure amount is numeric
    try:
        amount = float(amount)
    except Exception:
        raise ValueError(f"Invalid amount {amount} for product {upc}")

    # Connect to cart DB
    conn_cart = get_cart_conn()
    cursor_cart = conn_cart.cursor()

    # Fetch existing cart row
    row = cursor_cart.execute("SELECT * FROM cart WHERE upc=?", (upc,)).fetchone()

    if row:
        existing_qty = row["qty"] or 0
        existing_weight = row["weight"] or 0.0

        if is_produce:
            new_qty = 0
            new_weight = existing_weight + amount
            new_subtotal = new_weight * price
        else:
            new_qty = existing_qty + amount
            new_weight = new_qty * weight_per_unit
            new_subtotal = new_qty * price

        cursor_cart.execute(
            "UPDATE cart SET qty=?, subtotal=?, weight=?, resolved=? WHERE upc=?",
            (new_qty, new_subtotal, new_weight, 0, upc)
        )
    else:
        if is_produce:
            new_qty = 0
            new_weight = amount
            new_subtotal = new_weight * price
        else:
            new_qty = amount
            new_weight = amount * weight_per_unit
            new_subtotal = new_qty * price

        cursor_cart.execute(
            "INSERT INTO cart (upc, description, price, qty, subtotal, weight, produce, requires_verification, resolved) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (upc, product.get("description", ""), price, new_qty, new_subtotal, new_weight, product.get("produce", 0), product.get("requires_verification"), product.get("resolved", 0))
        )

    conn_cart.commit()
    conn_cart.close()

    # Build returned cart_product using actual updated values
    cart_product = {
        "product-added": {
            "upc": upc,
            "description": product.get("description", ""),
            "price": price,
            "qty": new_qty,
            "subtotal": new_subtotal,
            "weight": new_weight,
            "produce": is_produce,
            "requires_verification": product.get("requires_verification"),
            "resolved": 0
        }
    }

    data = {
        "upc": upc,
        "amount": amount
    }

    try:
        response = requests.post(f"{MASTER_URL}/update_stock", json=data)
        response.raise_for_status()
    except requests.RequestException as e:
        print(f"Warning: Could not update master stock for {upc}: {e}", file=sys.stderr)
    
    return cart_product

# Update cart quantity
# If items are removed through the screen display, return to local inventory and remove from cart
def update_cart(arg):
    MASTER_URL = get_master_url()
    data = arg
    upc = data.get("upc")
    remove_qty = data.get("remove_qty")
    if not upc or remove_qty is None:
        return {"status": "error", "message": "Invalid cart update"}

    conn_cart = get_cart_conn()
    cursor = conn_cart.cursor()
    row = cursor.execute("SELECT qty FROM cart WHERE upc=?", (upc,)).fetchone()
    cart_update = None
    if row:
        current_qty = row["qty"]
        new_qty = current_qty - remove_qty
        if new_qty <= 0:
            cursor.execute("DELETE FROM cart WHERE upc=?", (upc,))
            cart_update = None
        else:
            cursor.execute("UPDATE cart SET qty=? WHERE upc=?", (new_qty, upc))
            cart_update = {
                "upc": upc,
                "qty": new_qty
            }
        conn_cart.commit()
    conn_cart.close()

    payload = {
        "upc": upc,
        "amount": -remove_qty
    }
    response = requests.post(f"{MASTER_URL}/update_stock", json=payload)
    response.raise_for_status()

    return {"status": "success", "product-added": cart_update}

def qr_payment(cart_items):
    """
    cart_items: list of dicts, each dict has
        - description (str)
        - price (float)
        - qty (int)
        - produce (1 for weighted item)
        - weight (float, for produce)
    """
    try:
        if not cart_items:
            return {"status": "error", "message": "Cart is empty"}

        # Build line items
        line_items = []
        for item in cart_items:
            if item["produce"] == 1:
                quantity = str(item["weight"])  # weighted items can use decimal quantity
                amount = int(item["price"] * item["weight"] * 100)
            else:
                quantity = str(item.get("qty", 0))
                amount = int(item["price"] * item.get("qty", 0) * 100)

            line_items.append({
                "name": item["description"],
                "quantity": quantity,
                "base_price_money": {
                    "amount": amount,
                    "currency": "USD"
                }
            })

        # Create payment link with order
        result = square_client.checkout.payment_links.create(
            idempotency_key=str(uuid.uuid4()),
            order={
                "location_id": "LV6Z4TY832D53",
                "line_items": line_items
            }
        )

        if result.errors:
            err_msg = "; ".join([str(e) for e in result.errors])
            return {"status": "error", "message": err_msg}

        payment_link_info = result.payment_link
        return {
            "status": "success",
            "checkout_url": payment_link_info.url,
            "order_id": payment_link_info.order_id
        }

    except Exception as e:
        return {"status": "error", "message": str(e)}

# Add to the store local db
def add_complete_product(data):
    MASTER_URL = get_master_url()
    if not data or 'product' not in data:
        return {"status": "error", "message": "No product data provided"}

    try:
        response = requests.post(f"{MASTER_URL}/add_complete_product", json=data, timeout=10)
        response.raise_for_status()
        return response.json()
    except requests.exceptions.RequestException as e:
        return {"status": "error", "message": f"Request failed: {str(e)}"}

def update_product(data):
    MASTER_URL = get_master_url()
    try:
        response = requests.post(f"{MASTER_URL}/update_product", json=data)
        response.raise_for_status()
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"Error calling master server: {e}", file=sys.stderr)
        return {"status": "error", "message": str(e)}

def execute_sql(query):
    MASTER_URL = get_master_url()
    try:
        response = requests.post(f"{MASTER_URL}/execute_sql", json={"query": query})
        response.raise_for_status()
        return response.json()
    except requests.exceptions.RequestException as e:
        return {"error": f"Failed to execute SQL: {e}"}

def import_csv(csv_text):
    MASTER_URL = get_master_url()
    try:
        response = requests.post(f"{MASTER_URL}/import_csv", json={"csv": csv_text})
        response.raise_for_status()
        return response.json()
    except requests.exceptions.RequestException as e:
        return {"status": "error", "message": f"Failed to import CSV: {e}"}

def get_cart():
    conn = get_cart_conn()
    cursor = conn.cursor()
    rows = cursor.execute("SELECT * FROM cart").fetchall()
    conn.close()

    cart = [
        {
            "upc": row["upc"],
            "description": row["description"],
            "price": row["price"],
            "qty": row["qty"],
            "subtotal": row["subtotal"],
            "weight": row["weight"]
        }
        for row in rows
    ]
    return {"status": "success", "cart": cart}

def process_upc(upc):
    # simulate lookup
    product = {
        "upc": upc,
        "description": f"Flamin Hot Cheetos",
        "price": 2.99,
        "qty": 1,
        "subtotal": 5.98,
        "weight": 2.3,
        "produce": 0,
        "requires_verification" : 0
    }

    add_to_cart(product, 1)
    
    conn = get_cart_conn()
    row = conn.execute("SELECT * FROM cart WHERE upc=?", (upc,)).fetchone()
    conn.close()
    
    if row:
        product_update = {
            "upc": row["upc"],
            "description": row["description"],
            "price": row["price"],
            "qty": row["qty"],
            "subtotal": row["subtotal"],
            "weight": row["weight"],
            "produce": 0,
            "requires_verification" : row["requires_verification"],
            "resolved" : row["resolved"]
        }
    else:
        product_update = product

    return {"status": "success", "product-added": product_update}

def process_produce_upc(upc, weight):
    MASTER_URL = get_master_url()
    # Clean UPC and lookup product
    upc = str(upc).replace("UPC:", "").strip()
    payload = {"upc": upc}
    response = requests.post(f"{MASTER_URL}/lookup_product", json=payload)
    response.raise_for_status()
    resp_json = response.json()
    
    # Check if product exists
    if resp_json.get("status") != "success" or "product" not in resp_json:
        print(f"Product with UPC {upc} not found on master.", file=sys.stderr)
        return

    product = resp_json["product"]

    if not product:
        print(f"[DEBUG] No product found for UPC: '{upc}'", file=sys.stderr)
        return {"status": "error", "message": f"Product not found for UPC {upc}"}

    print(f"[DEBUG] Product found: {product}", file=sys.stderr)

    # Ensure weight is numeric
    weight = float(weight)

    # Add to cart (amount = weight for produce)
    add_to_cart(product, weight)

    # Fetch the row from the cart database
    cart_conn = get_cart_conn()
    cart_cursor = cart_conn.cursor()
    row = cart_cursor.execute("SELECT * FROM cart WHERE upc=?", (upc,)).fetchone()
    cart_conn.close()

    if row:
        return {
            "status": "success",
            "product": {
                "upc": row["upc"],
                "description": row["description"],
                "price": row["price"],
                "qty": row["qty"],         # total weight for produce
                "subtotal": row["subtotal"],
                "weight": row["weight"],
                "produce": row["produce"]
            }
        }
    else:
        return {"status": "error", "message": "Failed to retrieve cart row"}

def resolve_all_required_items():
    try:
        conn = get_cart_conn()
        cursor = conn.cursor()

        # Update all items that require verification
        cursor.execute("""
            UPDATE cart
            SET resolved = 1
            WHERE requires_verification = 1
        """)

        conn.commit()
        conn.close()

        return {"status": "success", "message": "All items requiring verification have been resolved."}
    except Exception as e:
        return {"status": "error", "message": str(e)}

def get_unresolved_items():
    try:
        conn = get_cart_conn()
        cursor = conn.cursor()

        # Select all items that require verification and are not resolved
        cursor.execute("""
            SELECT upc, description, qty, weight
            FROM cart
            WHERE requires_verification = 1 AND resolved = 0
        """)
        rows = cursor.fetchall()
        conn.close()

        # Convert rows to a list of dictionaries
        items = [dict(row) for row in rows]

        return {"status": "success", "items": items}
    except Exception as e:
        return {"status": "error", "message": str(e)}

def send_receipt(order_json):
    try:
        data = order_json
        email = data['email']
        order_items = data['items']
        total = data['total']
        order_id = data['orderId']

        msg = EmailMessage()
        msg['Subject'] = f"Receipt - Carte Diem"
        msg['From'] = "smartshoppingmodule@gmail.com"
        msg['To'] = email

        body = f"Thank you for your purchase!\n\nOrder ID: {order_id}\nItems:\n"
        for item in order_items:
            name = item.get('name', 'Unknown')
            qty = item.get('quantity', 1)
            price = item.get('price', 0)
            body += f" - {name} x{qty} - ${price}\n"

        body += f"\nTotal: ${total}\n\nHave a nice day and please come again!"
        msg.set_content(body)
        
        smtp_server = "smtp.gmail.com"
        smtp_port = 587
        username = "smartshoppingmodule@gmail.com"
        password = "cdxq plqa zcvm aqfi"

        # Debug logs go to stderr
        print(f"[cart_ops] Sending receipt to {email}", file=sys.stderr)

        with smtplib.SMTP(smtp_server, smtp_port) as server:
            server.set_debuglevel(1)  # SMTP debug output to stderr
            server.starttls()
            server.login(username, password)
            server.send_message(msg)

        # Only JSON to stdout
        return {"status": "sent", "email": email}

    except Exception as e:
        tb = traceback.format_exc()
        print(f"[cart_ops][ERROR] {tb}", file=sys.stderr)
        return {"status": "failed", "error": str(e)}

FUNCTIONS = {
    "get_setup" : get_setup,
    "save_setup" : save_setup,
    "start_session": start_session,
    "end_session": end_session,
    "calculate_total": calculate_total,
    "add_to_cart": add_to_cart,
    "qr_payment" : qr_payment,
    "add_complete_product" : add_complete_product,
    "update_product" : update_product,
    "import_csv" : import_csv,
    "get_cart_items": get_cart_items,
    "execute_sql" : execute_sql,
    "update_cart" : update_cart,
    "get_cart" : get_cart,
    "process_upc" : process_upc,
    "process_produce_upc" : process_produce_upc,
    "start_polling": start_polling,
    "get_order_status": get_order_status,
    "resolve_all_required_items" : resolve_all_required_items,
    "get_unresolved_items" : get_unresolved_items,
    "set_brightness" : set_brightness,
    "send_receipt" : send_receipt
}

if __name__ == "__main__":
    if len(sys.argv) > 1:
        func_name = sys.argv[1]
        args = sys.argv[2:]
        func = FUNCTIONS.get(func_name)
        if not func:
            print(json.dumps({"status": "error", "message": f"Unknown function: {func_name}"}))
            sys.exit(1)

        # Parse arguments safely
        parsed_args = []
        for a in args:
            try:
                # Try JSON first (for objects/arrays)
                parsed_args.append(json.loads(a))
            except json.JSONDecodeError:
                try:
                    parsed_args.append(int(a))
                except ValueError:
                    try:
                        parsed_args.append(float(a))
                    except ValueError:
                        parsed_args.append(a)

        try:
            result = func(*parsed_args)
            print(json.dumps(result))
        except Exception as e:
            print(json.dumps({"status": "error", "message": str(e)}))

    else:
        # asyncio.run(test_file_upload_from_txt("session.txt"))

        # Wrapper to run your async BLE main function in a thread
        def start_ble_listener():
            try:
                asyncio.run(main())
            except Exception as e:
                print(f"BLE listener error: {e}", file=sys.stderr)
        
        # Start BLE in a daemon thread
        threading.Thread(target=start_ble_listener, daemon=True).start()
        
        conn = sqlite3.connect("setting.db")
        cursor = conn.cursor()

        setup_complete = 0

        while setup_complete != 1:
            cursor.execute("SELECT setup_complete FROM setting WHERE id = 1")
            row = cursor.fetchone()
            setup_complete = row[0]
            time.sleep(1)

        COMPANY_URL = get_company_url()
        fetch_and_save_boundary(f"{COMPANY_URL}/boundaries")

        conn.close()

        # Start GPS parsing in a daemon thread
        threading.Thread(target=parse_gps, daemon=True).start()
    
        try:
            while True:
                cmd = sys.stdin.readline()

                if not cmd:
                    break
                
                cmd = cmd.strip()
                
                if cmd == "MEASURE_PROD_WEIGHT":
                    asyncio.get_event_loop().run_until_complete(
                        send_ble_command_async(cmd)
                    )
                    print(f"Measured Produce Weight Cmd Sent", file=sys.stderr)
                elif cmd == "PAY_START":
                    asyncio.get_event_loop().run_until_complete(
                        send_ble_command_async(cmd)
                    )
                    print(f"Payment Cmd Sent", file=sys.stderr)
                elif cmd == "M_CART":
                    asyncio.get_event_loop().run_until_complete(
                        send_ble_command_async(cmd)
                    )
                    print(f"M_CART Sent", file=sys.stderr)
                elif cmd == "CT_STOP":
                    asyncio.get_event_loop().run_until_complete(
                        send_ble_command_async(cmd)
                    )
                    print(f"CT_STOP Cmd Sent", file=sys.stderr)
                elif cmd == "CT_START":
                    asyncio.get_event_loop().run_until_complete(
                        send_ble_command_async(cmd)
                    )
                    print(f"CT_START Cmd Sent", file=sys.stderr)
                elif cmd == "CT_CLEAR":
                    asyncio.get_event_loop().run_until_complete(
                        send_ble_command_async(cmd)
                    )
                    print(f"CT_CLEAR Cmd Sent", file=sys.stderr)
                elif cmd == "TARE_PRODUCE_WEIGHT":
                    asyncio.get_event_loop().run_until_complete(
                        send_ble_command_async(cmd)
                    )
                    print(f"TARE_PRODUCE_WEIGHT Cmd Sent", file=sys.stderr)
                elif cmd == "TARE_CART_WEIGHT":
                    asyncio.get_event_loop().run_until_complete(
                        send_ble_command_async(cmd)
                    )
                    print(f"TARE_CART_WEIGHT Cmd Sent", file=sys.stderr)
                elif cmd == "IMU_CHECK_ACTIVITY":
                    asyncio.get_event_loop().run_until_complete(
                        send_ble_command_async(cmd)
                    )
                    print(f"IMU_CHECK_ACTIVITY Cmd Sent", file=sys.stderr)
                else:
                    print(f"Unknown command: {cmd}", file=sys.stderr)

        except KeyboardInterrupt:
            print("Exiting...")
            sys.exit(0)
