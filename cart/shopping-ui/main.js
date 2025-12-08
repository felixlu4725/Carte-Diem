const { app, BrowserWindow, ipcMain } = require("electron");
const path = require("path");
const { spawn } = require("child_process");

let mainWindow;
let cartProcess;

// --------------------- Helpers ---------------------
function sendProductAdded(product) {
  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.webContents.send("product-added", product);
  }
}

function sendPaymentReceived(payment) {
  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.webContents.send("payment-received", payment);
  }
}

function sendProduceWeightReceived(weight) {
  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.webContents.send("produce-weight-received", weight);
  }
}

function sendItemVerificationReceived(verification) {
  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.webContents.send("item-verification-received", verification);
  }
}

function sendIMUActivityReceived(activity) {
  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.webContents.send("imu-activity-received", activity);
  }
}

// --------------------- Window ---------------------
function createWindow() {
  mainWindow = new BrowserWindow({
    alwaysOnTop: false,
    frame: false,
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  mainWindow.maximize();
  
  const startUrl =
    process.env.ELECTRON_START_URL ||
    `file://${path.join(__dirname, "build", "index.html")}`;

  mainWindow.loadURL(startUrl);
}

app.commandLine.appendSwitch('enable-experimental-accessibility-support', 'true');

// --------------------- App lifecycle ---------------------
app.whenReady().then(() => {
  app.setAccessibilitySupportEnabled(true);
  createWindow();

  // --------------------- Continuous cart_ops.py ---------------------
  cartProcess = spawn("python3", [path.join(__dirname, "cart_ops.py")], {
    cwd: __dirname,
  });

  cartProcess.stdout.on("data", (data) => {
    const str = data.toString().trim();

    // CART_UPDATE
    if (str.startsWith("CART_UPDATE_JSON:")) {
      try {
        const parsed = JSON.parse(str.replace("CART_UPDATE_JSON:", ""));
        console.log("Parsed CART_UPDATE_JSON:", parsed);

        if (parsed.cart_update) {
          sendProductAdded(parsed.cart_update);
        }
      } catch (err) {
        console.error("Failed to parse CART_UPDATE_JSON:", err, str);
      }

    // PRODUCT_ADDED
    } else if (str.startsWith("PRODUCT_ADDED_JSON:")) {
      try {
        const parsed = JSON.parse(str.replace("PRODUCT_ADDED_JSON:", ""));
        console.log("Parsed PRODUCT_ADDED_JSON:", parsed);

        if (parsed["product-added"]) {
          sendProductAdded(parsed["product-added"]);
        }
      } catch (err) {
        console.error("Failed to parse PRODUCT_ADDED_JSON:", err, str);
      }

    // PAYMENT
    } else if (str.startsWith("PAYMENT_JSON:")) {
      try {
        const parsed = JSON.parse(str.replace("PAYMENT_JSON:", ""));
        console.log("Parsed PAYMENT_JSON:", parsed);

        if (parsed["payment-received"]) {
          console.log("Received payment:", parsed["payment-received"]);
          sendPaymentReceived(parsed["payment-received"]);
        }
      } catch (err) {
        console.error("Failed to parse PAYMENT_JSON:", err, str);
      } 

    // PRODUCE WEIGHT
    } else if (str.startsWith("PRODUCE_WEIGHT_JSON:")) {
      try {
        const parsed = JSON.parse(str.replace("PRODUCE_WEIGHT_JSON:", ""));
        console.log("Parsed PRODUCE_WEIGHT_JSON:", parsed);

        if (parsed["produce-weight-received"]) {
          console.log("Received produce weight:", parsed["produce-weight-received"]);
          sendProduceWeightReceived(parsed["produce-weight-received"]);
        }
      } catch (err) {
        console.error("Failed to parse PRODUCE_WEIGHT_JSON:", err, str);
      } 
    
    // ITEM VERIFICATION
    } else if (str.startsWith("ITEM_VERIFICATION_JSON:")) {
      try {
        const parsed = JSON.parse(str.replace("ITEM_VERIFICATION_JSON:", ""));
        console.log("Parsed ITEM_VERIFICATION_JSON:", parsed);

        if (parsed["item-verification-received"]) {
          // console.log("Received item verification:", parsed["item-verification-received"]);
          sendItemVerificationReceived(parsed["item-verification-received"]);
        }
      } catch (err) {
        console.error("Failed to parse ITEM_VERIFICATION_JSON:", err, str);
      } 
    // IMU Activity DATA
    } else if (str.startsWith("IMU_ACTIVITY_JSON:")) {
      try {
        const parsed = JSON.parse(str.replace("IMU_ACTIVITY_JSON:", ""));
        // console.log("Parsed IMU_ACTIVITY_JSON:", parsed);

        if (parsed["imu-activity-received"]) {
          console.log("Received imu activity:", parsed["imu-activity-received"]);
          sendIMUActivityReceived(parsed["imu-activity-received"]);
        }
      } catch (err) {
        console.error("Failed to parse IMU_ACTIVITY_JSON:", err, str);
      } 
    } else {
        console.log("Python stdout:", str);
      }
    });

  cartProcess.stderr.on("data", (data) => {
    console.error("[Python stderr]", data.toString());
  });

  cartProcess.on("exit", (code, signal) => {
    console.warn(`Python process exited with code ${code}, signal ${signal}`);
  });
});

// --------------------- run-python IPC handler ---------------------
ipcMain.handle("send-stdin-command", async (event, command) => {
  if (cartProcess && !cartProcess.killed) {
    cartProcess.stdin.write(command + "\n");
    return { status: "sent" };
  } else {
    return { status: "error", message: "Python process not running" };
  }
});

ipcMain.handle("run-python", async (event, { script, args }) => {
  return new Promise((resolve, reject) => {
    const pythonPath = "python3"; // adjust if needed
    const scriptPath = path.join(__dirname, script);

// Ensure all args are strings before sending to Python
const stringifiedArgs = (args || []).map((a) =>
  typeof a === "object" ? JSON.stringify(a) : String(a)
);

const pythonProcess = spawn(pythonPath, [scriptPath, ...stringifiedArgs], {
  cwd: __dirname,
});

    let stdout = "";
    let stderr = "";

    pythonProcess.stdout.on("data", (data) => {
      stdout += data.toString();
    });

    pythonProcess.stderr.on("data", (data) => {
      stderr += data.toString();
      console.error("[Python stderr]", data.toString());
    });

    pythonProcess.on("close", (code) => {
      try {
        const result = JSON.parse(stdout);

        if (result.product || result["product-added"]) {
          sendProductAdded(result.product || result["product-added"]);
        }

        resolve(result);
      } catch (err) {
        console.error("[Python JSON parse error]", err.message);
        console.error("[Python full stdout]", stdout);
        console.error("[Python stderr]", stderr);
        reject(err);
      }
    });

    pythonProcess.on("error", (err) => reject(err));
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});

app.on("activate", () => {
  if (BrowserWindow.getAllWindows().length === 0) createWindow();
});
