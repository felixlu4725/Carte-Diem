'''
This program will process an uploaded file to map out the image. (PDF, PNG, JPG)
    - Identify aisle/shelf locations (for proof of concept, our shelves are red. can be any color.)
    - Place evenly spaced tags on the shelves and return an image of it

We will then generate a 'rfid2map' .csv file which contains the RFID values of the mapped tags.
This will be used to map the rfid values to X,Y values.

Must run assignRFID

'''


import pandas as pd
import logging
logging.getLogger('matplotlib').setLevel(logging.WARNING)
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import seaborn as sb
import numpy as np
import cv2
from PIL import Image
import math
import fitz
import csv
import re
import os
import shutil
from pathlib import Path
from datetime import datetime
from statistics import mean
from scipy.stats import gaussian_kde
from matplotlib.colors import Normalize
import glob
from datetime import datetime
import os
import shutil
import json

#class which holds rfid tags and their xy position
class Tag:
    def __init__(self, shelf, tag_num, x, y, rfid=None):
        self.shelf = shelf
        self.tag_num = tag_num
        self.x = x
        self.y = y
        self.rfid = rfid

#class for CV. recognizing store & shelf boundaries
class Box:
    def __init__(self, x, y, w, h, num):
        self.x = x
        self.y = y
        self.w = w
        self.h = h
        self.num = num
        self.tags = []
#shelves & store location+dimensions
shelves = []
storeDim = Box(0,0,0,0,0)

#function to convert PDFs -> pictures if needed and make the image into CV-able object
def makeImage(path):
    ext = path.lower()
    if ext.endswith('.pdf'):
        doc = fitz.open(path)
        page = doc.load_page(0)
        pix = page.get_pixmap(dpi=200)
        img = Image.frombytes("RGB", [pix.width, pix.height], pix.samples)
        img_cv = np.array(img)
        img_corr = cv2.cvtColor(img_cv, cv2.COLOR_RGB2BGR)
        return img_corr
    else:
        img = cv2.imread(path, cv2.IMREAD_COLOR)
        return img


# Read file, process shelves, place tags
# path = file path / image file
# realWidth = width of the store so we can scale
# tagSpacing = space between each tag
def mapImage(path, realWidth, tagSpacing):
    img = makeImage(path)
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV) #convert to HSV


    #red mask (shelves. can be any color, just adjust these values)
    lower1 = np.array([0, 80, 60])
    upper1 = np.array([10, 255, 255])
    lower2 = np.array([170, 80, 60])
    upper2 = np.array([180, 255, 255])
    redmask = cv2.inRange(hsv, lower1, upper1) | cv2.inRange(hsv, lower2, upper2)
   
    #white mask (marks outside wall, for ratio)
    white1 = np.array([0,0,255])
    white2 = np.array([180, 50, 255])
    whitemask = cv2.inRange(hsv, white1, white2)
   
    redcontour, _ = cv2.findContours(redmask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    whitecontour, _ = cv2.findContours(whitemask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
   
    #identify shelves location
    for shelfNum, c in enumerate(redcontour):
        x, y, w, h = cv2.boundingRect(c)
        if w <= 1 or h <= 1: #filter out any red things that arent shelves
            continue
        shelves.append(Box(x, y, w, h, shelfNum))
        cv2.putText(img, str(shelfNum), (int(x+w/2), int(y+h/2)), cv2.FONT_HERSHEY_PLAIN, 3, (0,0,0), 1, cv2.LINE_AA)
        #cv2.rectangle(img, (x, y), (x+w, y+h), (0, 255, 0), 2)
        #print("Shelf at: ", x, y, "size: ", w, h)
        #cv2.circle(img, (x, y), 3, (255, 0,0), -1)
   
    #store boundary
    for c in whitecontour:
        x, y, w, h = cv2.boundingRect(c)
        #cv2.rectangle(img, (x, y), (x+w, y+h), (0, 255, 0), 2)
        #print("Shelf at: ", x, y, "size: ", w, h)
        storeDim.x = x
        storeDim.y = y
        storeDim.w = w
        storeDim.h = h
       
    scale = realWidth / storeDim.w
    wallSpacing = 36 #in inches
    for s in shelves:
        #go around the shelf if not near wall & place tags
        #place for width first
        numTagsW = math.floor((s.w * scale) / tagSpacing) + 1
        incW = s.w / numTagsW
        placedTags = 0
        for i in range(numTagsW+1):
            if (s.y - storeDim.y) > (wallSpacing*scale): #check if shelf at least X UNITs away from top wall
                cv2.circle(img, (int(s.x + (incW * i)), s.y), 10, (255, 0, 0), -1)
                cv2.putText(img, str(placedTags), (int(s.x+(incW*i)), s.y), cv2.FONT_HERSHEY_PLAIN, 3, (0,255,0), 1, cv2.LINE_AA)
                s.tags.append(Tag(s.num, placedTags, int(s.x+(incW*i)), s.y))
                placedTags = placedTags + 1


            if ((storeDim.y+storeDim.h) - (s.y+s.h)) > (wallSpacing*scale): #check if shelf at least X UNITs away from bottom wall
                cv2.circle(img, (int(s.x+(incW*i)), s.y+s.h), 10, (255, 0 ,0), -1)
                cv2.putText(img, str(placedTags), (int(s.x+(incW*i)), s.y+s.h), cv2.FONT_HERSHEY_PLAIN, 3, (0,255,0), 1, cv2.LINE_AA)
                s.tags.append(Tag(s.num, placedTags, int(s.x+(incW*i)), s.y+s.h))
                placedTags = placedTags + 1
           
        #place for height
        numTagsH = math.floor((s.h * scale) / tagSpacing) + 1
        incH = s.h / numTagsH
        for i in range(numTagsH+1):
            if (s.x - storeDim.x) > (wallSpacing* scale): #check if shelf X units away from left wall
                new_tag = Tag(s.num, placedTags, s.x, int(s.y+(incH*i)))
                exist = 0
                for tag in s.tags:
                    if tag.x == new_tag.x and tag.y == new_tag.y:
                        exist = 1
                if exist == 0:
                    cv2.circle(img, (s.x, int(s.y+(incH*i))), 10, (255, 0, 0), -1)
                    cv2.putText(img, str(placedTags), (s.x, int(s.y+(incH*i))), cv2.FONT_HERSHEY_PLAIN, 3, (0,255,0), 1, cv2.LINE_AA)
                    s.tags.append(new_tag)
                    placedTags = placedTags + 1
           
            if ((storeDim.x+storeDim.w)- (s.x+s.w)) > (wallSpacing*scale): #check if shelf X units away from right wall
                new_tag = Tag(s.num, placedTags, s.x+s.w, int(s.y+(incH*i)))
                exist = 0
                for tag in s.tags:
                    if tag.x == new_tag.x and tag.y == new_tag.y:
                        exist = 1
                if exist == 0:
                    cv2.putText(img, str(placedTags), (s.x+s.w, int(s.y+(incH*i))), cv2.FONT_HERSHEY_PLAIN, 3, (0,255,0), 1, cv2.LINE_AA)
                    cv2.circle(img, (s.x+s.w, int(s.y+(incH*i))), 10, (255, 0, 0), -1)
                    s.tags.append(new_tag)
                    placedTags = placedTags + 1
        text = f"Shelf {s.num} | X inc: {incW*scale:.2f} | Y inc: {incH*scale:.2f}"
        cv2.putText(img, text, (10,40+(s.num*40)), cv2.FONT_HERSHEY_PLAIN, 3, (0,0,0), 1, cv2.LINE_AA)


    print("store start: ", storeDim.x, storeDim.y)
    print("store dimensions: ", storeDim.w, storeDim.h)
    print(scale)
    #Return the processed image instead of displaying it
    # cv2.imshow("test", img)  # Commented out - causes crash in server environment
    # cv2.waitKey(0)
    return img


#insert RFID tag values mapped to tag and shelf numbers. takes in .csv file which maps RFID tag values to points on the schematic and assigns them for later use.
def assignRFID(file):
    with open(file, newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        reader.fieldnames = [h.strip() for h in reader.fieldnames]
        found = None
        for row in reader:
            for s in shelves:
                if s.num == int(str(row["Shelf"]).strip()):
                    for t in s.tags:
                        if t.tag_num == int(str(row["Tag"]).strip()):
                            t.rfid = str(row["RFID"]).strip()
                            
              
# USED FOR txt_to_csv              
# Matches lines like:
# Tag: E2801170000002076A508C74 | RSSI: 65 dBm | Antennna: 2 | Time: 1709943 ms
# to clean up input from ESP
LINE_RE = re.compile(
    r'^Tag:\s*(?P<tag>\S+)\s*\|\s*'
    r'RSSI:\s*(?P<rssi>-?\d+)\s*dBm\s*\|\s*'
    r'Antenn\w*:\s*(?P<antenna>\d+)\s*\|\s*'
    r'Time:\s*(?P<time>\d+)\s*ms\s*$'
)
#helper function
def txt_to_csv(in_path, out_path):
    rows = []
    buffer = ""
    brace_count = 0

    with open(in_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            buffer += line

            # Count braces
            brace_count += line.count("{")
            brace_count -= line.count("}")

            # If braces are balanced, try parsing
            if brace_count == 0 and buffer:
                # Clean trailing commas
                cleaned = re.sub(r",\s*([\]\}])", r"\1", buffer)

                try:
                    data = json.loads(cleaned)
                    for item in data.get("burst", []):
                        # skip empty tags
                        tag = item.get("tag", "").strip()
                        if not tag:
                            continue

                        try:
                            rows.append([
                                tag,
                                int(item.get("time", 0))
                            ])
                        except (ValueError, TypeError):
                            continue

                except json.JSONDecodeError:
                    # Could not parse, skip and print warning
                    print(f"⚠ Skipping invalid JSON fragment:\n{buffer}\n")

                buffer = ""  # reset for next JSON object

    # Write CSV
    with open(out_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["Tag", "Time_ms"])
        writer.writerows(rows)

    print(f"Wrote {len(rows)} rows to {out_path}")

# Path finding algorithm implemented using a Kalman filter which predicts the path given very noisy data  
class Kalman:
    def __init__ (self, dt=1.0):
        self.dt = dt
        self.x = np.zeros((4, 1))
       
        self.F = np.array([
            [1, 0, dt, 0],
            [0, 1, 0, dt],
            [0, 0, 1,  0],
            [0, 0, 0,  1]
        ])
   
        self.H = np.array([
            [1, 0, 0, 0],
            [0, 1, 0, 0]
        ])
       
        self.P = np.eye(4) * 500.0
       
        q = 1.0
        self.Q = np.array([
            [q, 0, 0, 0],
            [0, q, 0, 0],
            [0, 0, q, 0],
            [0, 0, 0, q]
        ])
       
        r = 5.0 # 1, 5, 20
        self.R = np.eye(2) * r
       
    def predict(self):
        self.x = self.F @ self.x
        self.P = self.F @ self.P @ self.F.T + self.Q
        return self.x


    def update(self, z):
        z = np.array(z).reshape(2, 1)
        y = z - (self.H @ self.x)
        S = self.H @ self.P @ self.H.T + self.R
        K = self.P @ self.H.T @ np.linalg.inv(S)
        self.x = self.x + K @ y
        I = np.eye(4)
        self.P = (I - K @ self.H) @ self.P
        return self.x

#helper function to find RFID tag. returns 0 if RFID reading is not a shelf tag
def findTag(RFID_val):
    for s in shelves:
        for t in s.tags:
            if t.rfid == RFID_val:
                return t
    return 0

           

# Base directory for generated images
GENERATED_IMAGES_DIR = 'generated_images'
LOG_BASE_DIR = 'cart_logs'

def get_image_save_path(cart_id, image_type, timestamp=None, session_number=None):
    """Generate organized path for saving heatmaps and path images.
    
    Args:
        cart_id: ID of the cart (e.g., 'CART_001')
        image_type: Type of image ('raw_path_points' or 'cart_heat_map')
        timestamp: datetime object (uses current time if not provided)
        session_number: Optional session number to include in filename
    
    Returns:
        str: Full path where the image should be saved
    """
    if timestamp is None:
        timestamp = datetime.now()
    
    # Create base directory
    os.makedirs(GENERATED_IMAGES_DIR, exist_ok=True)
    
    # Extract date components
    current_month = timestamp.strftime('%B_%Y')
    date_folder_name = timestamp.strftime('%m_%d_%y')
    
    # Archive old folders by month
    existing_folders = [f for f in os.listdir(GENERATED_IMAGES_DIR) 
                        if os.path.isdir(os.path.join(GENERATED_IMAGES_DIR, f)) 
                        and f not in [current_month] and '_' in f and len(f.split('_')) == 3]
    
    for folder in existing_folders:
        try:
            folder_date = datetime.strptime(folder, '%m_%d_%y')
            if folder_date.month != timestamp.month or folder_date.year != timestamp.year:
                month_archive = folder_date.strftime('%B_%Y')
                month_archive_path = os.path.join(GENERATED_IMAGES_DIR, month_archive)
                os.makedirs(month_archive_path, exist_ok=True)
                
                src = os.path.join(GENERATED_IMAGES_DIR, folder)
                dst = os.path.join(month_archive_path, folder)
                if not os.path.exists(dst):
                    shutil.move(src, dst)
        except ValueError:
            continue
    
    # Create date/cart folders
    date_folder_path = os.path.join(GENERATED_IMAGES_DIR, date_folder_name)
    cart_folder_path = os.path.join(date_folder_path, str(cart_id))
    os.makedirs(cart_folder_path, exist_ok=True)
    
    # Generate filename
    if session_number is not None:
        filename = f"{image_type}_{session_number}.png"
    else:
        # fallback to sequential numbering
        existing_images = [f for f in os.listdir(cart_folder_path) if f.startswith(image_type)]
        image_num = len(existing_images) + 1
        filename = f"{image_type}_{image_num}.png"
    
    file_path = os.path.join(cart_folder_path, filename)
    return file_path

def confirmPoint(point, tag):
    xoff, yoff = 0, 0
    edit = False
    if(point[0] > shelves[tag.shelf].x or point[0] < (shelves[tag.shelf].x + shelves[tag.shelf].w)): #inside shelf
        if abs(point[0] - shelves[tag.shelf].x) > abs(point[0] - (shelves[tag.shelf].x + shelves[tag.shelf].w)): 
            xoff = point[0] - shelves[tag.shelf].w
        else:
            xoff = point[0] + shelves[tag.shelf].w
        edit = True
    if(point[1] > shelves[tag.shelf].y or point[1] < (shelves[tag.shelf].x + shelves[tag.shelf].h)): #inside shelf
        if abs(point[1] - shelves[tag.shelf].y) > abs(point[1] - (shelves[tag.shelf].y + shelves[tag.shelf].h)):
            yoff = point[1] - shelves[tag.shelf].h
        else:
            yoff = point[1] + shelves[tag.shelf].h
        edit = True
    if edit == True:
        return (xoff, yoff)
    else:
        return point

class Path:
    
    def __init__ (self, cartNum):
        self.cartNum = cartNum
        self.path_arr = []

    def createPath(self, file):
        with open(file, newline='', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            burstX = []
            burstY = []
            burstTime = []
            t = None
            kPath = Kalman(dt=1.0)
            for row in reader:
                if len(burstX) < 10:
                    t = findTag(str(row["Tag"]))
                    if t == 0:
                        continue
                    else:
                        burstX.append(t.x)
                        burstY.append(t.y)
                        burstTime.append(int(row["Time_ms"]))
                else: #run kalman filter on burst
                    mx = np.median(burstX)
                    my = np.median(burstY)
                    mt = np.median(burstTime)
                    
                    kPath.predict()
                    estimate = kPath.update((mx, my))
                    x, y = estimate.flatten()[:2] 
                    x, y = confirmPoint((x, y), t) 
                    self.path_arr.append(np.array([x, y, mt]))
                   
                    burstX = []
                    burstY = []
                    burstTime = []
            if len(burstX) > 0: #add the remaining points after the 10 bursts
                mx = np.median(burstX)
                my = np.median(burstY)
                mt = np.median(burstTime)
                
                kPath.predict()
                estimate = kPath.update((mx, my))
                x, y = estimate.flatten()[:2]  
                x, y = confirmPoint((x, y), t)
                self.path_arr.append(np.array([x, y, mt]))
                
                burstX = []
                burstY = []
                burstTime = []
        for p in self.path_arr:
            print("X: ", p[0], " | Y: ", p[1], " | Time: ", p[2])
            
    def plotPathPoints(self, img, timestamp=None, save=True, show=False, session_number=None):
        """Plot path points with arrows and optionally save to organized folder."""
        overlay = img.copy()
        
        # Draw path points
        for p in self.path_arr:
            x, y = int(p[0]), int(p[1])
            cv2.circle(overlay, (x, y), 5, (0, 0, 0), -1)
        
        # Draw arrows between points
        for i in range(1, len(self.path_arr)):
            pt1 = (int(self.path_arr[i-1][0]), int(self.path_arr[i-1][1]))
            pt2 = (int(self.path_arr[i][0]), int(self.path_arr[i][1]))
            cv2.arrowedLine(overlay, pt1, pt2, (0, 255, 0), 2)
        
        saved_path = None
        if save:
            saved_path = get_image_save_path(self.cartNum, 'raw_path_points', timestamp, session_number=session_number)
            if os.path.exists(saved_path):
                logging.debug(f"Path image already exists: {saved_path}, skipping save.")
            else:
                cv2.imwrite(saved_path, overlay)
                logging.debug(f"Path image saved to: {saved_path}")
            
        if show and "DISPLAY" in os.environ:
            cv2.imshow("Cart Path", overlay)
            cv2.waitKey(0)
            cv2.destroyAllWindows()

        return saved_path

    def createHeatMap(self, img, timestamp=None, save=True, show=False, session_number=None):
        """Generate heatmap from path points and optionally save to organized folder.
        
        Args:
            img: Base image to overlay heatmap on
            cart_id: Cart identifier (e.g., 'CART_001')
            timestamp: datetime object for file organization
            save: Whether to save the image (default True)
            show: Whether to display the image (default False)
        
        Returns:
            str: Path where image was saved (if save=True), otherwise None
        """
        cpy = img.copy()
        x = [coord[0] for coord in self.path_arr]
        y = [coord[1] for coord in self.path_arr]

        if len(x) < 2:
            print("Not enough points to generate heatmap.")
            return None

        height, width = img.shape[:2]
        xx, yy = np.mgrid[0:width, 0:height]
        positions = np.vstack([xx.ravel(), yy.ravel()])
        

        kde = gaussian_kde(np.vstack([x, y]), bw_method=0.25)
        density = kde(positions).reshape(xx.shape)

        fig, ax = plt.subplots(figsize=(8, 10))
        ax.imshow(cpy, extent=[0, width, height, 0])

        heat = ax.imshow(
            density.T,
            cmap="turbo",
            alpha=0.4,
            extent=[0, width, height, 0],
            norm=Normalize(vmin=density.min(), vmax=density.max())
        )

        # cbar = plt.colorbar(heat, ax=ax)
        # cbar.set_label("Time")

        ax.scatter(x, y, c="green", s=10)

        #plt.savefig("cart_heat_map.png")
        
        #plt.show() #comment out for final
        saved_path = None
        if save:
            saved_path = get_image_save_path(self.cartNum, 'cart_heat_map', timestamp, session_number=session_number)
            if os.path.exists(saved_path):
                logging.debug(f"Heatmap already exists: {saved_path}, skipping save.")
            else:
                plt.savefig(saved_path)
                logging.debug(f"Heatmap saved to: {saved_path}")
        
        if show:
            plt.show()
        else:
            plt.close(fig)
        
        return saved_path

'''
Function to combine multiple paths into one heatmap
paths = array of paths
'''
def multHeatMap(paths, img, timestamp=None, save=True):
    cpy = img.copy()
    x = [] 
    y = []
    duration = []
    for p in paths:
        x.extend([coord[0] for coord in p.path_arr])
        y.extend([coord[1] for coord in p.path_arr])

    x = np.array(x)
    y = np.array(y)
    
    height, width = img.shape[:2]
    xx, yy = np.mgrid[0:width, 0:height]
    positions = np.vstack([xx.ravel(), yy.ravel()])
    
    x_flat = x.flatten()
    y_flat = y.flatten()
    kde = gaussian_kde([x_flat, y_flat], bw_method=0.25)
    density = kde(positions).reshape(xx.shape)
    
    fig, ax = plt.subplots(figsize=(8, 10))
    ax.imshow(cpy, extent=[0, width, height, 0])

    heat = ax.imshow(
        density.T,
        cmap="turbo",
        alpha=0.4,
        extent=[0, width, height, 0],
        norm=Normalize(vmin=density.min(), vmax=density.max())
    )
    #TO DO display carts that are being used
    colors = plt.cm.tab10(np.linspace(0, 1, len(paths)))

    for i, p in enumerate(paths):
        x_i = [coord[0] for coord in p.path_arr]
        y_i = [coord[1] for coord in p.path_arr]
        ax.scatter(x_i, y_i, c=[colors[i % 10]], s=8, label=f"Cart {p.cartNum}")

    ax.legend(loc="upper right", fontsize=6, title="Cart Paths")

    # Return the figure instead of saving
    return fig

'''
Create a path map with multiple cart paths
paths = array of Path objects
'''
def multPathMap(paths, img, timestamp=None, save=True):

    expanded_img = np.zeros((img.shape[0], img.shape[1] + 200, 3), dtype=np.uint8)
    expanded_img[:, :img.shape[1]] = img.copy()  # draw original image on left
    overlay = expanded_img.copy()

    legend_x = img.shape[1] + 20  # start inside the new space
    legend_y = 30
    offset = 35

    colors = [
        (0, 0, 255),     # Red
        (0, 255, 0),     # Green
        (255, 0, 0),     # Blue
        (0, 255, 255),   # Yellow
        (255, 0, 255),   # Purple
        (255, 255, 0),   # Cyan
    ]

    for idx, p in enumerate(paths):
        color = colors[idx % len(colors)]
        print(color)
        for data in p.path_arr:
            x, y = int(data[0]), int(data[1])
            cv2.circle(overlay, (x, y), 5, color, -1)
        for j in range(1, len(p.path_arr)):
            pt1 = (int(p.path_arr[j-1][0]), int(p.path_arr[j-1][1]))
            pt2 = (int(p.path_arr[j][0]), int(p.path_arr[j][1]))
            cv2.arrowedLine(overlay, pt1, pt2, color, 2)
        #make legend
        cy = legend_y + idx * offset
        cv2.circle(overlay, (legend_x, cy), 8, color, -1)
        cv2.putText(
            overlay,
            f"Cart {p.cartNum}",
            (legend_x + 20, cy + 5),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.7,
            (255, 255, 255),
            2,
            cv2.LINE_AA
        )

    # Return the image instead of saving
    return overlay


   
def process_daily_logs(store_schematic_path, store_width, tag_spacing, rfid_map_csv, target_date=None):
    """Process all log files for a specific date and generate heatmaps/path images.
    
    Args:
        store_schematic_path: Path to store schematic image
        store_width: Width of store in real units
        tag_spacing: Spacing between RFID tags
        rfid_map_csv: Path to RFID mapping CSV
        target_date: Date to process (MM_DD_YY format), defaults to today
    """
    if target_date is None:
        target_date = datetime.now().strftime('%m_%d_%y')
    
    # Load store schematic and RFID mappings
    img = mapImage(store_schematic_path, store_width, tag_spacing)
    assignRFID(rfid_map_csv)
    
    # Find all log files for the target date
    date_log_folder = os.path.join(LOG_BASE_DIR, target_date)
    
    if not os.path.exists(date_log_folder):
        print(f"No log files found for date: {target_date}")
        return
    
    # Get timestamp from date folder name
    try:
        timestamp = datetime.strptime(target_date, '%m_%d_%y')
    except ValueError:
        timestamp = datetime.now()
    
    # Process each cart's logs
    cart_folders = [f for f in os.listdir(date_log_folder) 
                   if os.path.isdir(os.path.join(date_log_folder, f))]
    
    print(f"\nProcessing {len(cart_folders)} carts for date {target_date}:")
    
    for cart_id in cart_folders:
        cart_folder_path = os.path.join(date_log_folder, cart_id)
        # Look for .csv files (converted from .txt) for processing
        csv_files = sorted(glob.glob(os.path.join(cart_folder_path, '*.csv')))
        
        if not csv_files:
            print(f"  {cart_id}: No CSV files found")
            continue
        
        print(f"  {cart_id}: Processing {len(csv_files)} CSV file(s)...")
        
        # Process each CSV file for this cart
        for csv_file in csv_files:
            try:
                # Create Path object and process the CSV
                cart_num = int(cart_id.split('_')[-1]) if '_' in cart_id else 1
                path = Path(cart_num)
                path.createPath(csv_file)
                
                # Generate and save images
                path.plotPathPoints(img, timestamp=timestamp, save=True, show=False)
                path.createHeatMap(img, timestamp=timestamp, save=True, show=False)
                
                print(f"    ✓ Processed: {os.path.basename(csv_file)}")
            except Exception as e:
                print(f"    ✗ Error processing {os.path.basename(csv_file)}: {str(e)}")
    
    print(f"\nCompleted processing for {target_date}")

if __name__ == "__main__":
    #file , actual width of store, spacing between each tag
    # blank = makeImage("eecscorner.jpg")
    img = mapImage("grocery_store.png", 161, 60)
    # txt_to_csv("cart_logs/12_06_25/CART_001/session_2.txt", "test.csv")
    # cv2.imwrite("mapped.jpg", img)
    # assignRFID("grocery_store_rfid2map.csv")
    # for s in shelves:
    #     for t in s.tags:
    #         print("Shelf Number: ", s.num, " | Tag Number: ", t.tag_num, " | X: ", t.x, " | Y: ", t.y, " | RFID: ", t.rfid)
    # p = Path(1)
    # p.createPath("cart_logs/12_05_25/CART_001/session_4.csv")
    # p.plotPathPoints(img)
    # p.createHeatMap(img)
    # p2 = Path(2)
    # p2.createPath("walkthrough1.csv")
    # p3 = Path(3)
    # p3.createPath("walkthrough3.csv")
    # select = [p, p2, p3]
    # multHeatMap(select, img)
    # multPathMap(select, img)