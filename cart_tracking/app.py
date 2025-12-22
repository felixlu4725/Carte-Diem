import json
import sqlite3
import subprocess
import shutil
import secrets
import os
import time
from datetime import datetime
from flask import Flask, jsonify, request, render_template_string
from flask_cors import CORS
from werkzeug.utils import secure_filename


app = Flask(__name__)
CORS(app)

# Master server configuration
MASTER_DB = 'master_stores.db'

frontend_process = subprocess.Popen(
    ["npm", "run", "dev"],
    cwd=os.path.join(os.getcwd(), "ct-website")  # run in ct-website folder
)

def init_master_database():
    #Initialize the master database to track all stores
    with sqlite3.connect(MASTER_DB) as conn:
        cursor = conn.cursor()
        
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS stores (
                store_key TEXT PRIMARY KEY,
                store_name TEXT NOT NULL,
                store_address TEXT NOT NULL,
                port INTEGER NOT NULL,
                status TEXT DEFAULT 'active',
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        ''')
        
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS boundaries (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                latitude REAL NOT NULL,
                longitude REAL NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        ''')
        
        conn.commit()
        print(" Master database initialized")

def get_next_available_port():
    #Get next available port from database to create a store server
    try:
        with sqlite3.connect(MASTER_DB) as conn:
            cursor = conn.cursor()
            cursor.execute('SELECT MAX(port) FROM stores WHERE status = "active"')
            result = cursor.fetchone()
            max_port = result[0] if result[0] else 442
            return max_port + 1
    except:
        return 442

#used for generating new store server
def get_stores_from_db():
    #Get all stores from database
    stores = {}
    try:
        with sqlite3.connect(MASTER_DB) as conn:
            cursor = conn.cursor()
            cursor.execute('SELECT store_key, store_name, store_address, port FROM stores WHERE status = "active"')
            for row in cursor.fetchall():
                store_key, store_name, store_address, port = row
                stores[store_key] = {
                    'name': store_name,
                    'address': store_address,
                    'port': port
                }
    except:
        pass
    return stores


#for when a store wants to join the CarpeDiem family <3
@app.route('/register_store', methods=['POST'])
def register_store():
    #Register a new store and create its child server
    try:
        # Handle both JSON and form data
        if request.content_type and 'multipart/form-data' in request.content_type:
            store_name = request.form.get('store_name', '').strip()
            store_address = request.form.get('store_address', '').strip()
        else:
            data = request.get_json()
            store_name = data.get('store_name', '').strip()
            store_address = data.get('store_address', '').strip()
        
        if not store_name or not store_address:
            return jsonify({'error': 'Store name and address are required'}), 400

        # Generate unique store key
        store_key = secrets.token_hex(4)  # 8-character hex key
        
        # Ensure key is unique (check database)
        existing_stores = get_stores_from_db()
        while store_key in existing_stores:
            store_key = secrets.token_hex(4)

        # No schematic processing during registration - stores will handle this themselves
        
        # Get next available port from database
        port = get_next_available_port()
        
        # Create store server file
        store_filename = f"store_{store_key}.py"
        shutil.copy2("cart_template.py", store_filename)
        
        # Launch store server: python3 store_id.py PORT STORE_KEY STORE_NAME STORE_ADDRESS
        process = subprocess.Popen([
            "python3", store_filename, str(port), store_key, store_name, store_address
        ], 
        stdout=subprocess.PIPE, 
        stderr=subprocess.PIPE,
        cwd=os.getcwd())
        
        frontend_process = subprocess.Popen(
            ["npm", "run", "dev", "--", f"--port={port}"],
            cwd=os.path.join(os.getcwd(), "ct-website")  # run in ct-website folder
        )
        
        # Give server a moment to start
        time.sleep(1)
        
        # Save to master database
        with sqlite3.connect(MASTER_DB) as conn:
            cursor = conn.cursor()
            cursor.execute('''
                INSERT INTO stores (store_key, store_name, store_address, port)
                VALUES (?, ?, ?, ?)
            ''', (store_key, store_name, store_address, port))
            conn.commit()
        
        print(f" New store registered: {store_name} @ {store_address} ({store_key}) on port {port}")
        
        return jsonify({
            'success': True,
            'store_key': store_key,
            'store_name': store_name,
            'store_port': port,
            'server_url': f'http://localhost:{port}/dashboard'
        })
        
    except Exception as e:
        return jsonify({'error': f'Server error: {str(e)}'}), 500

# Already have an account, log in with STORE ID
@app.route('/login_store', methods=['POST'])
def login_store():
    try:
        data = request.get_json()
        store_key = data.get('store_key', '').strip()

        if not store_key:
            return jsonify({'success': False, 'error': 'Store ID is required'}), 400

        with sqlite3.connect(MASTER_DB) as conn:
            cursor = conn.cursor()
            cursor.execute(
                "SELECT store_name, port FROM stores WHERE store_key=? AND status='active'",
                (store_key,)
            )
            row = cursor.fetchone()

        if row:
            store_name, port = row
            #send url to redirect to
            return jsonify({
                'success': True,
                'store_key': store_key,
                'store_name': store_name,
                'store_port': port,
                'redirect_url': f'http://localhost:{port}/dashboard'
            })
        else:
            return jsonify({'success': False, 'error': 'Invalid Store ID'}), 401

    except Exception as e:
        return jsonify({'success': False, 'error': f'Server error: {str(e)}'}), 500

if __name__ == '__main__':
    print(" CarteDiem Master Server Starting...")
    print(" This server manages store registration and creates individual store servers")
    
    # Initialize master database
    init_master_database()
    
    print(f"\n Master Server Endpoints:")
    print(f" POST /register_store       - Register new store")
    
    print(f"\n Master Server ready on http://localhost:5100")
    print(f" Visit http://localhost:5100 to register stores\n")
    
    app.run(host="0.0.0.0", port=5100, debug=False)
