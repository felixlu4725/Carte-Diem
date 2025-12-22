import json
import sqlite3
import statistics
import os
import shutil
import cv2
import glob
import numpy as np
from pathlib import Path as PathlibPath

from datetime import datetime

from flask import Flask, jsonify, request, send_file
from flask_cors import CORS
from werkzeug.utils import secure_filename

from rfidDataFilter import mapImage, process_daily_logs, GENERATED_IMAGES_DIR, multHeatMap, multPathMap, Path, assignRFID, txt_to_csv

import logging
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s [%(levelname)s] %(message)s',
    handlers=[
        logging.StreamHandler(),              # prints to terminal
        logging.FileHandler("process_logs.log")  # writes to a file
    ]
)

app = Flask(__name__)
CORS(app)

# Store configuration - will be set by command line args
STORE_KEY = None
DB_PATH = None
LOG_BASE_DIR = 'cart_logs'  # Base directory for all log files
IMAGES_DIR = 'images'

# Global cart session tracking - each cart has its own session ID
cart_sessions = {}  # {'001': 5, '002': 3, ...}


def init_database():
    #Initialize the SQLite database for this specific store
    with sqlite3.connect(DB_PATH) as conn:
        cursor = conn.cursor()
        
        # Create registered_carts table for cart management
        # Using "last seen" you can know the time stamp of cameras to find who had it last
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS registered_carts (
                hardware_id TEXT PRIMARY KEY,
                cart_id TEXT NOT NULL,
                first_registered TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        ''')
        
        # Create store_info table to store this store's information
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS store_info (
                store_name TEXT NOT NULL,
                store_address TEXT NOT NULL,
                port INTEGER NOT NULL,
                store_key TEXT NOT NULL,
                processed_schematic_path TEXT
            )
        ''')
        
        # Create boundaries table for GPS boundaries
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS boundaries (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                latitude REAL NOT NULL,
                longitude REAL NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        ''')
        
        conn.commit()
        print(" Database initialized")

def save_store_info_to_db(store_name, store_address, port, store_key):
    #Saving the store information when the server is created
    try:
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            
            # Insert store info (should only happen once per store)
            cursor.execute('''
                INSERT INTO store_info (store_name, store_address, port, store_key)
                VALUES (?, ?, ?, ?)
            ''', (store_name, store_address, port, store_key))
            conn.commit()
            return True
    except Exception as e:
        return False

def get_cart_by_hardware_id(hardware_id):
    #Get cart info by hardware ID - returns cart_id if found, None if new
    try:
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute('SELECT cart_id FROM registered_carts WHERE hardware_id = ?', (hardware_id,))
            result = cursor.fetchone()
            return result[0] if result else None
    except:
        return None

def count_registered_carts():
    #Count how many carts are registered to this store, to ea
    try:
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute('SELECT COUNT(*) FROM registered_carts')
            return cursor.fetchone()[0]
    except:
        return 0

def register_new_cart(hardware_id, cart_id):
    #Register a new cart in the database
    try:
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute('''
                INSERT INTO registered_carts (hardware_id, cart_id)
                VALUES (?, ?)
            ''', (hardware_id, cart_id))
            conn.commit()
            return True
    except:
        return False

def update_cart_last_seen(hardware_id):
    #Update last seen timestamp for a cart"""
    try:
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute('''
                UPDATE registered_carts 
                SET last_seen = CURRENT_TIMESTAMP 
                WHERE hardware_id = ?
            ''', (hardware_id,))
            conn.commit()
    except:
        pass

def get_hardware_id_by_cart_id(cart_id):
    #Get hardware_id for a given cart_id
    try:
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute('SELECT hardware_id FROM registered_carts WHERE cart_id = ?', (cart_id,))
            result = cursor.fetchone()
            return result[0] if result else None
    except:
        return None


# First Log from CART_001 on 09/18 : Creates 09_18_25/CART_001/
# Second Log from CART_001 on 09/18 : Uses existing 09_18_25/CART_001/ and adds the new file
# First Log from CART_002 on 09/18 : Uses existing 09_18_25/ and creates 09_18_25/CART_002/
def organize_txt_file(txt_file, txt_timestamp, cart_id):
    """Organize log files into date/cart folders with session numbering.
    
    Args:
        txt_file: The uploaded file object
        txt_timestamp: datetime object of when the log was created
        cart_id: ID of the cart (e.g., 'CART_001')
    
    Returns:
        tuple: (txt_file_path, session_number)
    """
    # Create base log directory if it doesn't exist
    os.makedirs(LOG_BASE_DIR, exist_ok=True)
    
    # Extract date components
    current_month = txt_timestamp.strftime('%B_%Y')  # like : 'November_2025'
    date_folder_name = txt_timestamp.strftime('%m_%d_%y')  # like : '11_21_25' (filesystem safe)
    
    # Check if we need to archive existing folders into a month folder
    existing_folders = [f for f in os.listdir(LOG_BASE_DIR) 
                       if os.path.isdir(os.path.join(LOG_BASE_DIR, f)) 
                       and f not in [current_month] and '_' in f and len(f.split('_')) == 3]
    
    if existing_folders:
        # Check if any existing folder is from a different month
        for folder in existing_folders:
            try:
                folder_date = datetime.strptime(folder, '%m_%d_%y')
                # If folder is from a different month than current log
                if folder_date.month != txt_timestamp.month or folder_date.year != txt_timestamp.year:
                    # Archive all date folders into month folder
                    month_archive = folder_date.strftime('%B_%Y')
                    month_archive_path = os.path.join(LOG_BASE_DIR, month_archive)
                    os.makedirs(month_archive_path, exist_ok=True)
                    
                    # Move all date folders to the month archive
                    all_date_folders = [f for f in os.listdir(LOG_BASE_DIR) 
                                       if os.path.isdir(os.path.join(LOG_BASE_DIR, f)) 
                                       and f != month_archive and '_' in f and len(f.split('_')) == 3]
                    
                    for date_folder in all_date_folders:
                        src = os.path.join(LOG_BASE_DIR, date_folder)
                        dst = os.path.join(month_archive_path, date_folder)
                        if not os.path.exists(dst):
                            shutil.move(src, dst)
                    break
            except ValueError:
                # Skip folders that don't match date format
                continue
    
    # Create the target date folder
    date_folder_path = os.path.join(LOG_BASE_DIR, date_folder_name)
    os.makedirs(date_folder_path, exist_ok=True)
    
    # Create cart-specific subfolder within the date folder
    cart_folder_path = os.path.join(date_folder_path, cart_id)
    os.makedirs(cart_folder_path, exist_ok=True)
    
    # Count existing .txt files to determine session number
    existing_txt_files = [f for f in os.listdir(cart_folder_path) if f.endswith('.txt')]
    session_number = len(existing_txt_files) + 1
    
    # Generate filename with session number
    base_filename = f"session_{session_number}.txt"
    txt_file_path = os.path.join(cart_folder_path, base_filename)
    
    # Save the .txt file
    txt_file.save(txt_file_path)
    
    return txt_file_path, session_number

def handle_cart_connection(hardware_id):
    #Handle cart connection - returns (cart_id, is_reconnection)"""
    #do this for when register either an exisiting cart again or new cart which needs a new cart_id
    # Check if cart already exists
    existing_cart_id = get_cart_by_hardware_id(hardware_id)
    
    if existing_cart_id:
        # Existing cart reconnecting
        update_cart_last_seen(hardware_id)
        print(f" Cart {existing_cart_id} reconnected ({hardware_id})")
        return existing_cart_id, True
    else:
        # New cart - assign next available cart_id
        next_cart_num = count_registered_carts() + 1
        cart_id = f"CART_{next_cart_num:03d}"
        
        if register_new_cart(hardware_id, cart_id):
            print(f" New cart registered: {cart_id} ({hardware_id})")
            return cart_id, False
        else:
            return None, False

@app.route('/input_schematic', methods=['POST'])
def input_schematic():
    try:
        store_schematic = request.files.get('store_schematic')
        store_width = float(request.form.get('store_width'))
        store_tag_spacing = float('60')
        
        if not store_schematic:
            return jsonify({'error': 'Store schematic required'})
        allowed_extensions = { 'png', 'pdf', 'bmp' }
        file_ext = store_schematic.filename.split('.')[-1].lower()
        if file_ext not in allowed_extensions:
            return jsonify({'error': 'Invalid file type'}), 400

        filename = secure_filename(store_schematic.filename)
        
        # Create images directory for this store
        images_dir = f'images/{STORE_KEY}'
        
        # Clean up old images directory if it exists
        if os.path.exists(images_dir):
            shutil.rmtree(images_dir)
        os.makedirs(images_dir)
        
        # Save original image temporarily for RFID processing
        temp_original_path = f'{images_dir}/temp_original_{filename}'
        store_schematic.save(temp_original_path)
        
        # Verify file was saved
        if not os.path.exists(temp_original_path) or os.path.getsize(temp_original_path) == 0:
            return jsonify({'error': 'Failed to save uploaded image'}), 400

        # Process the schematic and return the RFID-tagged image
        try:
            RFID_image = mapImage(temp_original_path, store_width, store_tag_spacing)
        except Exception as map_error:
            # Clean up temp file
            if os.path.exists(temp_original_path):
                os.remove(temp_original_path)
            return jsonify({'error': f'Failed to process image: {str(map_error)}'}), 500

        # Save the RFID-processed image
        RFID_filename = f'processed_{filename}'
        RFID_path = f'{images_dir}/{RFID_filename}'
        
        try:
            cv2.imwrite(RFID_path, RFID_image)
        except Exception as write_error:
            return jsonify({'error': f'Failed to save processed image: {str(write_error)}'}), 500
        
        # Remove temporary original file
        os.remove(temp_original_path)

        # Update database with RFID image path only
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute('''
                UPDATE store_info 
                SET processed_schematic_path = ?
                WHERE store_key = ?
            ''', (RFID_path, STORE_KEY))
            conn.commit()
        
        return jsonify({
            'success': True,
            'processed_image_path': RFID_path
        })

    except Exception as e:
        return jsonify({'error': f'Server error: {str(e)}'}), 500

@app.route('/register_cart', methods=['POST'])
def register_cart():
    #Register or reconnect a cart using hardware ID (either employee manually enters or esp will autosend
    try:
        data = request.get_json()
        hardware_id = data.get('hardware_id')
        
        if not hardware_id:
            return jsonify({'error': 'Missing hardware_id'}), 400
        
        # Handle cart connection (new or reconnection)
        cart_id, is_reconnection = handle_cart_connection(hardware_id)
        
        if cart_id:
            return jsonify({
                'success': True,
                'cart_id': cart_id
            })
        else:
            return jsonify({'error': 'Failed to register cart'}), 500
            
    except Exception as e:
        import traceback
        return jsonify({
            'error': str(e),
            'traceback': traceback.format_exc()
        }), 500

@app.route('/cart/data', methods=['POST'])
def receive_cart_data():
    """Receive .txt files from carts via WiFi and organize them by date and cart.
    
    Expected format:
    - File upload with key 'txt_file'
    - Required form data 'cart_id' (e.g., 'CART_001')
    - Optional form data 'timestamp' (ISO format) - if not provided, uses current time
    
    Files are named session_1.txt, session_2.txt, etc. based on number of sessions.
    """
    
    try:
        # Check if txt file is present
        if 'txt_file' not in request.files:
            return jsonify({'error': 'No txt file provided'}), 400
        
        txt_file = request.files['txt_file']
        
        if txt_file.filename == '':
            return jsonify({'error': 'Empty filename'}), 400
        
        # TEST to see file received correctly
        # txt_file.seek(0)
        # contents = txt_file.read().decode('utf-8')

        # with open("test.txt", "w", encoding="utf-8") as f:
        #     f.write(contents)

        # txt_file.seek(0)
        
        hardware_id = request.form.get('hardware_id')
        if not hardware_id:
            return jsonify({'error': 'hardware_id is required'}), 400

        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute(
                "SELECT cart_id FROM registered_carts WHERE hardware_id = ?",
                (hardware_id,)
            )
            row = cursor.fetchone()
            if not row:
                return jsonify({'error': f'No registered cart found for hardware_id {hardware_id}'}), 404
            
            cart_id = row[0]

        # Get timestamp from form data or use current time
        timestamp_str = request.form.get('timestamp')
        if timestamp_str:
            try:
                txt_timestamp = datetime.fromisoformat(timestamp_str)
            except ValueError:
                txt_timestamp = datetime.now()
        else:
            txt_timestamp = datetime.now()
        
        # Organize and save the log file (returns txt_path, session_number)
        txt_path, session_number = organize_txt_file(txt_file, txt_timestamp, cart_id)
        
        return jsonify({
            'success': True,
            'cart_id': cart_id,
            'session_number': session_number,
            'file_path': txt_path,
            'timestamp': txt_timestamp.isoformat(),
            'message': f'Session {session_number} saved successfully'
        })
        
    except Exception as e:
        return jsonify({'error': f'Server error: {str(e)}'}), 500


# Send store information from database
@app.route('/store/info', methods=['GET'])
def get_store_info():
    try:
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute('SELECT store_name, store_address, port, store_key FROM store_info LIMIT 1')
            result = cursor.fetchone()
            if result:
                store_name, store_address, port, store_key = result
                return jsonify({
                    'success': True,
                    'store_name': store_name,
                    'store_address': store_address,
                    'port': port,
                    'store_key': store_key
                })
            else:
                return jsonify({'success': False, 'error': 'Store info not found'}), 404
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/store/schematic/processed', methods=['GET'])
def get_processed_schematic():
    try:
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute('SELECT processed_schematic_path FROM store_info WHERE store_key = ?', (STORE_KEY,))
            result = cursor.fetchone()
            
            if result and result[0]:
                return send_file(result[0])
            else:
                return jsonify({'error': 'No processed schematic found'}), 404
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/images/list', methods=['GET'])
def list_generated_images():
    """List all generated images organized by date and cart.
    
    Optional query parameters:
    - date: Filter by specific date (format: MM_DD_YY)
    - cart_id: Filter by specific cart ID
    
    Returns JSON structure of available images.
    """
    try:
        if not os.path.exists(GENERATED_IMAGES_DIR):
            return jsonify({'images': {}, 'message': 'No images generated yet'})
        
        date_filter = request.args.get('date')
        cart_filter = request.args.get('cart_id')
        
        images_structure = {}
        
        # Walk through the directory structure
        for root, dirs, files in os.walk(GENERATED_IMAGES_DIR):
            # Get relative path from base
            rel_path = os.path.relpath(root, GENERATED_IMAGES_DIR)
            path_parts = rel_path.split(os.sep)
            
            # Skip if it's just the base directory
            if rel_path == '.':
                continue
            
            # Determine if this is a date folder or cart folder
            if len(path_parts) == 1:
                # This is a date folder (could be MM_DD_YY or Month_Year)
                date_folder = path_parts[0]
                if date_filter and date_folder != date_filter:
                    continue
            elif len(path_parts) == 2:
                # This is a cart folder within a date folder
                date_folder = path_parts[0]
                cart_folder = path_parts[1]
                
                if date_filter and date_folder != date_filter:
                    continue
                if cart_filter and cart_folder != cart_filter:
                    continue
                
                # Collect image files
                image_files = [f for f in files if f.endswith(('.png', '.jpg', '.jpeg'))]
                
                if image_files:
                    if date_folder not in images_structure:
                        images_structure[date_folder] = {}
                    
                    images_structure[date_folder][cart_folder] = {
                        'heatmaps': [f for f in image_files if f.startswith('cart_heat_map')],
                        'paths': [f for f in image_files if f.startswith('raw_path_points')]
                    }
            elif len(path_parts) == 3:
                # This is inside a month archive (Month_Year/MM_DD_YY/CART_XXX)
                month_folder = path_parts[0]
                date_folder = path_parts[1]
                cart_folder = path_parts[2]
                
                if date_filter and date_folder != date_filter:
                    continue
                if cart_filter and cart_folder != cart_filter:
                    continue
                
                image_files = [f for f in files if f.endswith(('.png', '.jpg', '.jpeg'))]
                
                if image_files:
                    archive_key = f"{month_folder}/{date_folder}"
                    if archive_key not in images_structure:
                        images_structure[archive_key] = {}
                    
                    images_structure[archive_key][cart_folder] = {
                        'heatmaps': [f for f in image_files if f.startswith('cart_heat_map')],
                        'paths': [f for f in image_files if f.startswith('raw_path_points')]
                    }
        
        return jsonify({
            'success': True,
            'images': images_structure
        })
        
    except Exception as e:
        return jsonify({'error': f'Server error: {str(e)}'}), 500

@app.route('/images/<path:image_path>', methods=['GET'])
def get_generated_image(image_path):
    """Retrieve a specific generated image.
    
    Path format examples:
    - /images/09_18_25/CART_001/heatmap_001.png
    - /images/September_2025/09_18_25/CART_001/path_001.png

    or 

    - /images/combined_heat_map_20231121_153045.png
    """
    try:
        # GENERATED_IMAGES_DIR 
        full_path = os.path.join(GENERATED_IMAGES_DIR, image_path)
        if not os.path.exists(full_path):
            # IMAGES_DIR
            full_path = os.path.join(IMAGES_DIR, image_path)

        full_path = os.path.abspath(full_path)
        base_generated = os.path.abspath(GENERATED_IMAGES_DIR)
        base_images = os.path.abspath(IMAGES_DIR)

        if not (full_path.startswith(base_generated) or full_path.startswith(base_images)):
            return jsonify({'error': 'Invalid path'}), 400

        if not os.path.exists(full_path):
            return jsonify({'error': 'Image not found'}), 404

        return send_file(full_path, mimetype='image/png')

    except Exception as e:
        return jsonify({'error': f'Server error: {str(e)}'}), 500
    
@app.route('/process_logs', methods=['POST'])
def process_logs():
    """Process all .txt log files and generate heatmaps/path images."""
    try:
        store_width = 161
        tag_spacing = 60
        
        logging.debug("Starting process_logs...")

        # Get store schematic from database
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute(
                'SELECT processed_schematic_path FROM store_info WHERE store_key = ?',
                (STORE_KEY,)
            )
            result = cursor.fetchone()
            if not result or not result[0]:
                logging.warning("No store schematic found.")
                return jsonify({'error': 'No store schematic found. Please upload one first.'}), 400
            store_schematic = result[0]
            logging.debug(f"Store schematic path: {store_schematic}")
        
        # Load store schematic and RFID mappings
        img = mapImage(store_schematic, store_width, tag_spacing)
        logging.debug("Loaded store schematic image.")
        
        # Verify image was loaded correctly
        if img is None or not hasattr(img, 'shape'):
            logging.error("Failed to load store schematic image")
            return jsonify({'error': 'Failed to load store schematic'}), 500
            
        assignRFID('grocery_store_rfid2map.csv')
        logging.debug("RFID mapping assigned.")
        
        # Find all date folders in cart_logs
        if not os.path.exists(LOG_BASE_DIR):
            logging.warning("No log files found.")
            return jsonify({'error': 'No log files found'}), 404
        
        date_folders = [f for f in os.listdir(LOG_BASE_DIR) 
                        if os.path.isdir(os.path.join(LOG_BASE_DIR, f)) and '_' in f]
        logging.debug(f"Found date folders: {date_folders}")
        
        total_processed = 0
        results = {}
        
        for date_folder in date_folders:
            logging.debug(f"Processing date folder: {date_folder}")
            date_folder_path = os.path.join(LOG_BASE_DIR, date_folder)
            cart_folders = [f for f in os.listdir(date_folder_path) 
                            if os.path.isdir(os.path.join(date_folder_path, str(f)))]
            results[date_folder] = {}
            
            for cart_id in cart_folders:
                logging.debug(f"Processing cart folder: {cart_id}")
                cart_folder_path = os.path.join(date_folder_path, str(cart_id))
                txt_files = sorted([f for f in os.listdir(cart_folder_path) if f.endswith('.txt')])
                
                if not txt_files:
                    logging.debug(f"No txt files in {cart_folder_path}")
                    continue
                
                results[date_folder][cart_id] = []
                
                for txt_file in txt_files:
                    txt_path = os.path.join(cart_folder_path, txt_file)
                    csv_path = txt_path.replace('.txt', '.csv')
                    
                    # Convert .txt to .csv if needed
                    if not os.path.exists(csv_path):
                        try:
                            logging.debug(f"Converting {txt_path} to CSV...")
                            txt_to_csv(txt_path, csv_path)
                        except Exception as e:
                            logging.error(f"Could not convert {txt_path} to CSV: {str(e)}")
                            continue
                    
                    # Use full cart_id instead of extracting number
                    session_name = txt_file.replace('.txt', '')
                    
                    # Extract session number for image filenames
                    session_number = session_name.replace('session_', '')

                    try:
                        # Pass the full cart_id (e.g., "CART_001")
                        path = Path(cart_id)
                        path.createPath(csv_path)
                        
                        try:
                            timestamp = datetime.strptime(date_folder, '%m_%d_%y')
                        except ValueError:
                            timestamp = datetime.now()
                        
                        # Generate images
                        path.plotPathPoints(img.copy(), timestamp=timestamp, save=True, show=False, session_number=session_number)
                        path.createHeatMap(img.copy(), timestamp=timestamp, save=True, show=False, session_number=session_number)
                        
                        results[date_folder][cart_id].append(session_name)
                        total_processed += 1
                        logging.debug(f"Processed {date_folder}/{cart_id}/{session_name}")
                        
                    except Exception as e:
                        logging.exception(f"Error processing {txt_path}: {str(e)}")
                        continue
                            
        logging.debug(f"Finished processing. Total sessions: {total_processed}")
        return jsonify({
            'success': True,
            'total_processed': total_processed,
            'results': results,
            'message': f'Successfully processed {total_processed} sessions'
        })
        
    except Exception as e:
        logging.exception("Server error in process_logs")
        return jsonify({'error': f'Server error: {str(e)}'}), 500


@app.route('/boundaries', methods=['POST'])
def save_boundaries():
    """Save GPS boundaries to database"""
    try:
        data = request.get_json()
        boundaries = data.get('boundaries', [])
        
        if not boundaries:
            return jsonify({'error': 'Boundaries list is required'}), 400
        
        # Clear existing boundaries
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute('DELETE FROM boundaries')
            
            # Insert new boundaries
            for coord in boundaries:
                if len(coord) != 2:
                    return jsonify({'error': 'Each boundary must have latitude and longitude'}), 400
                latitude, longitude = coord
                cursor.execute(
                    'INSERT INTO boundaries (latitude, longitude) VALUES (?, ?)',
                    (latitude, longitude)
                )
            
            conn.commit()
        
        return jsonify({
            'success': True,
            'count': len(boundaries)
        })
        
    except Exception as e:
        return jsonify({'error': f'Server error: {str(e)}'}), 500


@app.route('/boundaries', methods=['GET'])
def get_boundaries():
    """Get GPS boundaries from database as a closed polygon"""
    try:
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute('SELECT longitude, latitude FROM boundaries ORDER BY id')
            boundaries = [[lon, lat] for lon, lat in cursor.fetchall()]
        
        # Close the polygon by appending the first point to the end
        if boundaries:
            boundaries.append(boundaries[0])
        
        return jsonify({
            'type': 'Polygon',
            'boundaries': boundaries
        })
        
    except Exception as e:
        return jsonify({'error': f'Server error: {str(e)}'}), 500


@app.route('/combined_data', methods=['GET'])
def combined_data():
    """Generate combined heatmap and path map from session paths.
    
    Query parameters:
    - sessions: Comma-separated list of session paths
      Example: ?sessions=09_18_25/CART_001/session_1,09_18_25/CART_002/session_1,09_22_25/CART_003/session_9
    
    Saves images to generated_images/ and returns their URLs.
    """
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        
        # Get sessions from query parameter
        sessions_param = request.args.get('sessions', '')
        if not sessions_param:
            return jsonify({'error': 'sessions parameter is required'}), 400
        
        session_paths = [s.strip() for s in sessions_param.split(',')]
        
        if len(session_paths) == 0:
            return jsonify({'error': 'At least one session path is required'}), 400
        
        if len(session_paths) > 10:
            return jsonify({'error': 'Maximum 10 sessions allowed'}), 400
        
        store_width = 1073
        tag_spacing = 50
        
        # Get store schematic from database
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute('SELECT processed_schematic_path FROM store_info WHERE store_key = ?', (STORE_KEY,))
            result = cursor.fetchone()
            if not result or not result[0]:
                return jsonify({'error': 'No store schematic found. Please upload one first.'}), 400
            store_schematic = result[0]
        
        # Load store schematic and RFID mappings
        img = mapImage(store_schematic, store_width, tag_spacing)
        assignRFID('rfid2map.csv')
        
        # Load Path objects from session files
        paths = []
        for session_path in session_paths:
            # session_path format: "09_18_25/CART_001/session_1"
            parts = session_path.split('/')
            if len(parts) != 3:
                print(f"Warning: Invalid session path format: {session_path}")
                continue
            
            date_folder, cart_id, session_name = parts
            
            # Build the full path to the .txt file
            txt_file = os.path.join(LOG_BASE_DIR, date_folder, cart_id, f"{session_name}.txt")
            
            if not os.path.exists(txt_file):
                print(f"Warning: {txt_file} not found")
                continue
            
            # Convert .txt to .csv if CSV doesn't exist
            csv_file = txt_file.replace('.txt', '.csv')
            if not os.path.exists(csv_file):
                try:
                    txt_to_csv(txt_file, csv_file)
                    print(f" Converted {txt_file} to {csv_file}")
                except Exception as e:
                    print(f"Warning: Could not convert {txt_file} to CSV: {str(e)}")
                    continue
            
            # Extract cart number from cart_id (e.g., "CART_001" -> 1)
            cart_num = str(int(cart_id.split('_')[-1])) if '_' in cart_id else "1"
            
            # Create Path object and process the CSV
            path = Path(cart_num)
            path.createPath(csv_file)
            paths.append(path)
        
        if len(paths) == 0:
            return jsonify({'error': 'No valid sessions found to process'}), 400
        
        # Delete old combined images
        os.makedirs(GENERATED_IMAGES_DIR, exist_ok=True)
        old_combined_files = glob.glob(os.path.join('images', 'combined_heat_map_*.png')) + \
                           glob.glob(os.path.join('images', 'combined_path_map_*.png'))
        for old_file in old_combined_files:
            try:
                os.remove(old_file)
                print(f"Deleted old combined image: {old_file}")
            except Exception as e:
                print(f"Warning: Could not delete {old_file}: {str(e)}")
        
        # Generate combined heatmap (returns matplotlib figure)
        heatmap_fig = multHeatMap(paths, img)
        
        # Save heatmap to generated_images/
        timestamp_str = datetime.now().strftime('%Y%m%d_%H%M%S')
        heatmap_filename = f'combined_heat_map_{timestamp_str}.png'
        heatmap_path = os.path.join('images', heatmap_filename)
        heatmap_fig.savefig(heatmap_path)
        plt.close(heatmap_fig)
        print(f"Combined heatmap saved to: {heatmap_path}")
        
        # Generate combined path map (returns cv2 image array)
        pathmap_img = multPathMap(paths, img)
        
        # Save pathmap to generated_images/
        pathmap_filename = f'combined_path_map_{timestamp_str}.png'
        pathmap_path = os.path.join('images', pathmap_filename)
        cv2.imwrite(pathmap_path, pathmap_img)
        print(f"Combined path map saved to: {pathmap_path}")
        
        return jsonify({
            'success': True,
            'heatmap_url': f'/images/{heatmap_filename}',
            'pathmap_url': f'/images/{pathmap_filename}',
            'sessions_processed': len(paths)
        })
        
    except Exception as e:
        return jsonify({'error': f'Server error: {str(e)}'}), 500

@app.route('/carts_registered', methods=['GET'])
def get_cart_info():
    """
    Retrieve hardware_id and cart_id for all carts

    Returns:
    {
        "carts": [
            {"hardware_id": "9c:13:9e:a8:5d:f5", "cart_id": "CART_001"},
            {"hardware_id": "1c:25:70:67:10:10", "cart_id": "CART_002"},
        ]
    }
    """
    try:
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT hardware_id, cart_id FROM registered_carts")
            rows = cursor.fetchall()
        
        carts = [{"hardware_id": row[0], "cart_id": row[1]} for row in rows]
        return jsonify({"carts": carts}), 200

    except Exception as e:
        return jsonify({"error": f"Server error: {str(e)}"}), 500
    
if __name__ == '__main__':
    import sys

    # Get basic arguments: python3 store_KEY.py PORT STORE_KEY [RFID_DATA_JSON]
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 5001 #unique port
    store_key = sys.argv[2] if len(sys.argv) > 2 else 'store key' #unique store key
    store_name = sys.argv[3] if len(sys.argv) > 3 else 'store name' #store name
    store_address = sys.argv[4] if len(sys.argv) > 4 else 'store address' #store address
    rfid_data_json = sys.argv[5] if len(sys.argv) > 5 else '[]' #RFID based on store schematic

    
    # Configure store globals
    STORE_KEY = store_key
    DB_PATH = f'store_{store_key}.db'
    # Parse RFID positions from JSON
    try:
        rfid_positions = json.loads(rfid_data_json)
    except:
        rfid_positions = []
    
    print(f" Store {store_key} starting on port {port}")
    
    # Initialize database
    init_database()
    
    # Save store information to database for frontend access
    save_store_info_to_db(store_name, store_address, port, store_key)
    
    print("\n Cart Endpoints:")
    print(" POST /register_cart            - Register/reconnect cart with hardware_id")
    print(" POST /input_schematic          - Take in a file and output RFID positions for cart tracking")
    print(" POST /cart/data                - Upload .txt files (multipart/form-data: txt_file' + 'cart_id')")
    print("                                  Files are numbered as session_1.txt, session_2.txt, etc.")
    print(" POST /process_logs             - Process log files and generate heatmaps/path images")
    print(" GET /combined_data             - Generate combined heatmap & path map from sessions")
    print("                                  (?sessions=date/cart/session,date/cart/session, max 10)")
    print(" POST /boundaries               - Save GPS boundaries (JSON: {boundaries: [[lat, lon], ...]})")
    print(" GET /store/info                - Get store information")
    print(" GET /store/schematic/processed - Get processed schematic with RFID tags")
    print(" GET /images/list               - List all generated images (optional: ?date=MM_DD_YY&cart_id=CART_XXX)")
    print(" GET /images/<path>             - Get specific generated image")
    print(" GET /boundaries                - Get GPS boundaries as closed polygon with type")
    
    app.run(host="0.0.0.0", port=port)
