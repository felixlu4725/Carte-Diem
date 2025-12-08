const { contextBridge, ipcRenderer } = require("electron");

let paymentListener = null;

contextBridge.exposeInMainWorld("electronAPI", {
  // Run a Python function and get JSON result
  runPython: (script, args) => ipcRenderer.invoke("run-python", { script, args }),

  sendStdinCommand: async (command) => {
    ipcRenderer.invoke('send-stdin-command', command);
  },

  // Subscribe to UPC scan events
  onProductAdded: (callback) => {
    ipcRenderer.on("product-added", (event, product) => {
      callback(product);
    });
  },

  // Subscribe to payment events
  onPaymentReceived: (callback) => {
    paymentListener = (event, payment) => callback(payment);
    ipcRenderer.on("payment-received", paymentListener);
  },

  removePaymentListener: () => {
    if (paymentListener) {
      ipcRenderer.removeListener("payment-received", paymentListener);
      paymentListener = null;
    }
  },

  // Subscribe to produce weight events
  onProduceWeightReceived: (callback) => {
    produceWeightListener = (event, weight) => callback(weight);
    ipcRenderer.on("produce-weight-received", produceWeightListener);
  },

  removeProduceWeightReceived: () => {
    if (produceWeightListener) {
      ipcRenderer.removeListener("produce-weight-received", produceWeightListener);
      produceWeightListener = null;
    }
  },
  
  // Subscribe to item verification events
  onItemVerificationReceived: (callback) => {
    ipcRenderer.on("item-verification-received", (event, verification) => {
      callback(verification);
    });
  },

  // Subscribe to misc data events
  onIMUActivityReceived: (callback) => {
    ipcRenderer.on("imu-activity-received", (event, activity) => {
      callback(activity);
    });
  },
});