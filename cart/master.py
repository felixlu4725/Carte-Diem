from flask import Flask, request, jsonify
from flask_cors import CORS
import sqlite3
from flask_socketio import SocketIO
import subprocess
import os
import time
import platform
import csv
import io

master = Flask(__name__)
sio = SocketIO(master)
CORS(master)

is_windows = platform.system() == "Windows"

# Run React development server
# frontend_process = subprocess.Popen(
#     ["npm", "run", "start"],
#     shell=is_windows,
#     # run in shopping-ui folder
#     cwd=os.path.join(os.getcwd(), "shopping-ui"),
#     stdout=subprocess.DEVNULL,
#     stderr=subprocess.DEVNULL
# )

time.sleep(1)

# Electron app interface
# electron_process = subprocess.Popen(
#     ["npm", "run", "electron"],
#     shell=is_windows,
#     cwd=os.path.join(os.getcwd(), "shopping-ui")  # run in shopping-ui folder
# )

time.sleep(1)

DB_FILE = "products.db"

def get_db_connection():
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    return conn

# look up product from database
@master.route('/lookup_product', methods=['POST'])
def lookup_product():
    try:
        data = request.get_json()
        if not data or "upc" not in data:
            return jsonify({"status": "error", "message": "No UPC provided"}), 400

        upc = data["upc"]
        conn = get_db_connection()
        product_row = conn.execute("SELECT * FROM Products WHERE upc=?", (upc,)).fetchone()

        restricted_row = conn.execute(
            "SELECT 1 FROM restricted_items WHERE upc=?", (upc,)
        ).fetchone()
        requires_verification = bool(restricted_row)

        conn.close()

        if product_row:
            product = dict(product_row)

            product["requires_verification"] = requires_verification
            return jsonify({"status": "success", "product": product})
        else:
            return jsonify({"status": "error", "message": f"Product {upc} not found"}), 404
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@master.route('/lookup_rfid', methods=['POST'])
def lookup_rfid():
    try:
        data = request.get_json()
        if not data or "tags" not in data:
            return jsonify({"status": "error", "message": "No tags provided"}), 400

        tags = data["tags"]
        if not isinstance(tags, list) or not tags:
            return jsonify({"status": "error", "message": "Tags must be a non-empty list"}), 400

        conn = get_db_connection()

        placeholders = ",".join("?" for _ in tags)
        query = f"""
            SELECT pr.rfid_id, pr.upc
            FROM ProductRFIDs pr
            WHERE pr.rfid_id IN ({placeholders})
        """
        rows = conn.execute(query, tags).fetchall()
        conn.close()

        if not rows:
            return jsonify({
                "status": "success",
                "message": "No products found for provided RFID tags",
                "products": []
            })

        products = [{"rfid_id": row["rfid_id"], "upc": row["upc"]} for row in rows]

        return jsonify({
            "status": "success",
            "products": products
        })

    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500
    
@master.route('/update_stock', methods=['POST'])
def update_stock():
    data = request.get_json()
    upc = data.get("upc")
    amount = data.get("amount")

    amount = float(amount)

    conn_store = get_db_connection()
    cursor_store = conn_store.cursor()
    row = cursor_store.execute("SELECT qty FROM Products WHERE upc=?", (upc,)).fetchone()
    if not row:
        conn_store.close()
        return jsonify({"error": "Product not found"}), 404

    stock_qty = row[0] or 0
    new_stock = max(stock_qty - amount, 0)
    cursor_store.execute(
        "UPDATE Products SET qty=? WHERE upc=?",
        (new_stock, upc)
    )
    conn_store.commit()
    conn_store.close()

    return jsonify({"upc": upc, "old_stock": stock_qty, "new_stock": new_stock, "status": "success"})

@master.route('/add_complete_product', methods=['POST'])
def add_complete_product():
    data = request.get_json()
    print(data)
    if not data or 'product' not in data:
        return jsonify({"status": "error", "message": "No product data provided"}), 400

    product = data['product']

    try:
        conn = get_db_connection()
        cursor = conn.cursor()

        # Insert or update product
        cursor.execute("""
            INSERT INTO Products (upc, brand, description, qty, price, weight, produce)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(upc) DO UPDATE SET
                brand=excluded.brand,
                description=excluded.description,
                qty=excluded.qty,
                price=excluded.price,
                weight=excluded.weight,
                produce=excluded.produce
        """, (
            product['upc'], product['brand'], product['description'], product['qty'],
            product['price'], product['weight'], product['produce']
        ))

        # Categories
        cursor.execute("DELETE FROM ProductCategories WHERE upc = ?", (product['upc'],))
        for category in data.get('categories', []):
            cursor.execute("INSERT OR IGNORE INTO Categories (name) VALUES (?)", (category,))
            cursor.execute("SELECT id FROM Categories WHERE name = ?", (category,))
            cat_row = cursor.fetchone()
            if cat_row:
                cat_id = cat_row[0]
                cursor.execute("INSERT INTO ProductCategories (upc, category_id) VALUES (?, ?)",
                               (product['upc'], cat_id))

        # Aisle locations
        cursor.execute("DELETE FROM AisleLocations WHERE upc = ?", (product['upc'],))
        aisle_locs = data.get('aisleLocation', [])
        if not isinstance(aisle_locs, list):
            aisle_locs = [aisle_locs]
        for loc in aisle_locs:
            cursor.execute("""
                INSERT INTO AisleLocations (upc, aisle, section, side, description)
                VALUES (?, ?, ?, ?, ?)
            """, (
                product['upc'], loc.get('aisle', ''), loc.get('section', ''),
                loc.get('side', ''), loc.get('description', '')
            ))

        # Images
        cursor.execute("DELETE FROM Images WHERE upc = ?", (product['upc'],))
        for img in data.get('images', []):
            cursor.execute("""
                INSERT INTO Images (upc, size, url)
                VALUES (?, ?, ?)
            """, (
                product['upc'], img.get('size', ''), img.get('url', '')
            ))

        # Update RFID
        cursor.execute("DELETE FROM ProductRFIDs WHERE upc = ?", (product['upc'],))
        rfid_tags = data.get('rfidTags', [])
        if isinstance(rfid_tags, str):
            rfid_tags = [rfid_tags]
        for tag in rfid_tags:
            if tag:  # skip empty strings
                cursor.execute("""
                    INSERT INTO ProductRFIDs (upc, rfid_id)
                    VALUES (?, ?)
                """, (product['upc'], tag))
            
        conn.commit()
        return jsonify({"status": "success", "upc": product['upc']}), 200

    except Exception as e:
        conn.rollback()
        return jsonify({"status": "error", "message": str(e)}), 500

    finally:
        conn.close()

@master.route('/update_product', methods=['POST'])
def update_product():
    data = request.get_json()
    if not data or not data.get("upc"):
        return jsonify({"status": "error", "message": "No UPC provided"}), 400

    upc = data["upc"]
    conn = None
    try:
        conn = get_db_connection()
        cursor = conn.cursor()

        # Upsert product
        cursor.execute("""
            INSERT INTO Products (
                upc, brand, description, qty, price, weight, produce
            )
            VALUES (?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(upc) DO UPDATE SET
                brand = excluded.brand,
                description = excluded.description,
                qty = excluded.qty,
                price = excluded.price,
                weight = excluded.weight,
                produce = excluded.produce
        """, (
            upc, data.get("brand"), data.get("description"), data.get("qty"),
            data.get("price"), data.get("weight"), data.get("produce")
        ))

        # Update categories
        cursor.execute("DELETE FROM ProductCategories WHERE upc = ?", (upc,))
        for category in data.get("categories", []):
            cursor.execute("INSERT OR IGNORE INTO Categories (name) VALUES (?)", (category,))
            cursor.execute("SELECT id FROM Categories WHERE name = ?", (category,))
            cat_row = cursor.fetchone()
            if cat_row:
                cat_id = cat_row[0]
                cursor.execute("INSERT INTO ProductCategories (upc, category_id) VALUES (?, ?)", (upc, cat_id))

        # Update aisle locations
        cursor.execute("DELETE FROM AisleLocations WHERE upc = ?", (upc,))
        for loc in data.get("aisleLocations", []):
            cursor.execute("""
                INSERT INTO AisleLocations (upc, aisle, section, side, description)
                VALUES (?, ?, ?, ?, ?)
            """, (
                upc, loc.get("aisle"), loc.get("section"), loc.get("side"), loc.get("description")
            ))

        # Update images
        cursor.execute("DELETE FROM Images WHERE upc = ?", (upc,))
        for img in data.get("images", []):
            cursor.execute("""
                INSERT INTO Images (upc, size, url)
                VALUES (?, ?, ?)
            """, (upc, img.get("size"), img.get("url")))

        # Update RFID tags
        cursor.execute("DELETE FROM ProductRFIDs WHERE upc = ?", (upc,))
        rfid_tags = data.get("rfidTags", [])

        if isinstance(rfid_tags, str):
            rfid_tags = [rfid_tags]

        for tag in rfid_tags:
            if tag:
                cursor.execute(
                    "INSERT INTO ProductRFIDs (upc, rfid_id) VALUES (?, ?)",
                    (upc, tag)
                )

        conn.commit()

        # Fetch updated product with related info
        cursor.execute("SELECT * FROM Products WHERE upc = ?", (upc,))
        row = cursor.fetchone()
        product = dict(row) if row else {}

        cursor.execute("""
            SELECT c.name FROM Categories c
            JOIN ProductCategories pc ON c.id = pc.category_id
            WHERE pc.upc = ?
        """, (upc,))
        product["categories"] = [r["name"] for r in cursor.fetchall()]

        cursor.execute("SELECT * FROM AisleLocations WHERE upc = ?", (upc,))
        product["aisleLocations"] = [dict(r) for r in cursor.fetchall()]

        cursor.execute("SELECT * FROM Images WHERE upc = ?", (upc,))
        product["images"] = [dict(r) for r in cursor.fetchall()]

        cursor.execute("SELECT rfid_id FROM ProductRFIDs WHERE upc = ?", (upc,))
        product["rfidTags"] = [r["rfid_id"] for r in cursor.fetchall()]

        return jsonify({"status": "success", "product": product}), 200

    except Exception as e:
        if conn:
            conn.rollback()
        return jsonify({"status": "error", "message": f"Failed to update product {upc}: {e}"}), 500

    finally:
        if conn:
            conn.close()

@master.route("/execute_sql", methods=["POST"])
def execute_sql():
    data = request.get_json()
    if not data or "query" not in data:
        return jsonify({"error": "No query provided"}), 400

    query = data["query"].strip()
    if not query:
        return jsonify({"error": "Empty query"}), 400

    conn = None
    try:
        conn = get_db_connection()
        cursor = conn.cursor()

        if query.lower().startswith("select"):
            cursor.execute(query)
            rows = cursor.fetchall()
            results = [dict(row) for row in rows]
            return jsonify({"results": results})
        else:
            cursor.executescript(query)
            conn.commit()
            return jsonify({"results": "Query executed successfully"})

    except sqlite3.Error as e:
        return jsonify({"error": str(e)}), 500
    except Exception as e:
        return jsonify({"error": str(e)}), 500
    finally:
        if conn:
            conn.close()

@master.route("/import_csv", methods=["POST"])
def import_csv():
    try:
        data = request.get_json()
        csv_text = data.get("csv") if data else None
        if not csv_text:
            return jsonify({"status": "error", "message": "No CSV content provided", "imported": 0}), 400

        stream = io.StringIO(csv_text)
        csv_reader = csv.DictReader(stream)

        conn = get_db_connection()
        cursor = conn.cursor()

        imported_count = 0
        products_dict = {}

        # Parse CSV into structured dict
        for row in csv_reader:
            upc = row.get("UPC")
            if not upc:
                continue

            if upc not in products_dict:
                products_dict[upc] = {
                    'upc': upc,
                    'brand': row.get('Brand', ''),
                    'description': row.get('Description', ''),
                    'qty': int(row.get('Qty') or 0),
                    'price': float(row.get('Price') or 0.0),
                    'weight': float(row.get('Weight') or 0.0),
                    'produce': int(row.get('Produce') or 0),
                    'categories': [],
                    'aisleLocations': [],
                    'images': [],
                    'rfidTags' : []
                }

                if row.get('Categories'):
                    categories = [c.strip() for c in row['Categories'].split(",") if c.strip()]
                    products_dict[upc]['categories'] = categories

                if row.get('Aisle'):
                    products_dict[upc]['aisleLocations'].append({
                        'aisle': row.get('Aisle', ''),
                        'section': row.get('Section', ''),
                        'side': row.get('Side', ''),
                        'description': row.get('AisleDescription', '')
                    })

            if row.get('RFIDTags'):
                for tag in row['RFIDTags'].split(","):
                    t = tag.strip()
                    if t and t.upper() != "NONE" and t not in products_dict[upc]['rfidTags']:
                        products_dict[upc]['rfidTags'].append(t)

            if row.get('ImageSize') and row.get('ImageURL'):
                products_dict[upc]['images'].append({
                    'size': row['ImageSize'],
                    'url': row['ImageURL']
                })

        # Insert/update products
        for upc, pdata in products_dict.items():
            # Upsert product
            cursor.execute("""
                INSERT INTO Products (
                    upc, brand, description, qty, price, weight, produce
                ) VALUES (?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(upc) DO UPDATE SET
                    brand = excluded.brand,
                    description = excluded.description,
                    qty = excluded.qty,
                    price = excluded.price,
                    weight = excluded.weight,
                    produce = excluded.produce
            """, (upc, pdata['brand'], pdata['description'], pdata['qty'],
                  pdata['price'], pdata['weight'], pdata['produce']))

            # Categories
            cursor.execute("DELETE FROM ProductCategories WHERE upc = ?", (upc,))
            for category in pdata['categories']:
                cursor.execute("INSERT OR IGNORE INTO Categories (name) VALUES (?)", (category,))
                cursor.execute("SELECT id FROM Categories WHERE name = ?", (category,))
                cat_id = cursor.fetchone()[0]
                cursor.execute("INSERT INTO ProductCategories (upc, category_id) VALUES (?, ?)", (upc, cat_id))

            # Aisle locations
            cursor.execute("DELETE FROM AisleLocations WHERE upc = ?", (upc,))
            for loc in pdata['aisleLocations']:
                cursor.execute("""
                    INSERT INTO AisleLocations (upc, aisle, section, side, description)
                    VALUES (?, ?, ?, ?, ?)
                """, (upc, loc['aisle'], loc['section'], loc['side'], loc['description']))

            # Images
            cursor.execute("DELETE FROM Images WHERE upc = ?", (upc,))
            for img in pdata['images']:
                cursor.execute("INSERT INTO Images (upc, size, url) VALUES (?, ?, ?)",
                               (upc, img['size'], img['url']))

            cursor.execute("DELETE FROM ProductRFIDs WHERE upc = ?", (upc,))
            rfid_tags = pdata.get('rfidTags', [])
            if isinstance(rfid_tags, str):
                rfid_tags = [rfid_tags]

            for tag in rfid_tags:
                if tag.strip():  # skip blanks
                    cursor.execute("""
                        INSERT INTO ProductRFIDs (upc, rfid_id)
                        VALUES (?, ?)
                    """, (upc, tag.strip()))

            imported_count += 1

        conn.commit()
        conn.close()
        return jsonify({"status": "success", "imported": imported_count})

    except Exception as e:
        if conn:
            conn.rollback()
        return jsonify({"status": "error", "message": str(e), "imported": 0}), 500

    finally:
        if conn:
            conn.close()
    
if __name__ == "__main__":
    sio.run(master, host="0.0.0.0", port=80)