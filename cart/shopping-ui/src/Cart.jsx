import React, { useState, useEffect, useRef } from "react";
import { useNavigate, useLocation } from "react-router-dom";
import { QRCodeCanvas } from "qrcode.react";
import { io } from "socket.io-client";
import "./Cart.css";

const Cart = ({
  items,
  setItems,
  showWorkerHelp,
  setShowWorkerHelp,
  verificationStatus,
  setVerificationStatus,
}) => {
  const navigate = useNavigate();
  const location = useLocation();

  useEffect(() => {
    console.log("Current path:", location.pathname);
  }, [location]);

  const [showCheckoutModal, setShowCheckoutModal] = useState(false);
  const [showResultModal, setShowResultModal] = useState(false);
  const [selectedPayment, setSelectedPayment] = useState("");
  const [paymentUrl, setPaymentUrl] = useState("");
  const [showRemoveConfirmModal, setShowRemoveConfirmModal] = useState(false);
  const [itemToRemove, setItemToRemove] = useState(null);
  const [removeQty, setRemoveQty] = useState(1);
  const [showThankYouModal, setShowThankYouModal] = useState(false);
  const [thankYouTime, setThankYouTime] = useState(null);
  const [searchQuery, setSearchQuery] = useState("");
  const [searchResults, setSearchResults] = useState([]);
  const [weighingItem, setWeighingItem] = useState(null);
  const [showWeighingPrompt, setShowWeighingPrompt] = useState(false);
  const [currentWeight, setCurrentWeight] = useState(0);
  const [calculatedPrice, setCalculatedPrice] = useState(0);
  const [showPaymentModal, setShowPaymentModal] = useState(false);
  const [paymentMessage, setPaymentMessage] = useState("");
  const [showUnresolvedModal, setShowUnresolvedModal] = useState(false);
  const [showVerificationToast, setShowVerificationToast] = useState(false);
  const [showVerificationBlock, setShowVerificationBlock] = useState(false);
  const [helpRequested, setHelpRequested] = useState(false);
  const weighingItemRef = useRef(null);
  const [imuStatus, setImuStatus] = useState("MOVING");
  const imuStatusRef = useRef(imuStatus);
  const cancelWeighingRef = useRef(false);
  const [showWeightStep, setShowWeightStep] = useState(false);

  const [orderId, setOrderId] = useState(null);
  const [showQRCodeModal, setShowQRCodeModal] = useState(false);

  const [showEmailModal, setShowEmailModal] = useState(false);
  const [receiptEmail, setReceiptEmail] = useState("");

  // Admin password modal
  const [showAdminLogin, setShowAdminLogin] = useState(false);
  const [adminPassword, setAdminPassword] = useState("");

  useEffect(() => {
    const fetchCartItems = async () => {
      try {
        const result = await window.electronAPI.runPython("cart_ops.py", [
          "get_cart_items",
        ]);
        if (result.status === "success") {
          setItems(result.items);
        } else {
          console.error("Failed to fetch cart items:", result.message);
        }
      } catch (err) {
        console.error("Error fetching cart items:", err);
      }
    };

    // Only fetch when on /cart
    if (location.pathname === "/cart") {
      fetchCartItems();
    }
  }, [location.pathname, setItems]);

  useEffect(() => {
    window.electronAPI.onProductAdded((product) => {
      document.body.classList.add("flash");
      setTimeout(() => document.body.classList.remove("flash"), 500);

      setItems((prevItems) => {
        const existing = prevItems.find((i) => i.upc === product.upc);
        if (existing) {
          return prevItems.map((i) =>
            i.upc === product.upc ? { ...i, ...product } : i
          );
        } else {
          return [product, ...prevItems];
        }
      });
    });
  }, []);

  useEffect(() => {
    const handler = async (payment) => {
      console.log("Payment received:", payment);

      if (payment.status === "success") {
        setShowPaymentModal(false);
        setShowThankYouModal(true);
        setThankYouTime(new Date());

        // setTimeout(() => {
        //   setItems([]);
        //   navigate("/");
        // }, 8000);

        setPaymentMessage("✅ Payment successful!");

        try {
          const result = await window.electronAPI.runPython("cart_ops.py", [
            "end_session",
          ]);
          console.log("Python end session result:", result);

          await window.electronAPI.sendStdinCommand("CT_STOP");
        } catch (err) {
          console.error("Failed to clear backend cart via Python:", err);
        }
      } else {
        setPaymentMessage("❌ Payment failed. Please try again.");

        setTimeout(async () => {
          setPaymentMessage("👉 Please tap your card on the reader now.");

          try {
            console.log("Retrying card payment: sending PAY_START...");
            await window.electronAPI.sendStdinCommand("PAY_START");
          } catch (err) {
            console.error("Failed to restart PAY_START:", err);
          }
        }, 2000);
      }
    };

    window.electronAPI.onPaymentReceived(handler);

    return () => {
      window.electronAPI.removePaymentListener(handler);
    };
  }, []);

  useEffect(() => {
    imuStatusRef.current = imuStatus;
  }, [imuStatus]);

  useEffect(() => {
    window.electronAPI.onIMUActivityReceived((activity) => {
      console.log("IMU Activity received:", activity);

      try {
        let status;

        if (typeof activity === "string") {
          const match = activity.match(/\[IMU\]\s*(\w+)/);
          status = match ? match[1].toUpperCase() : activity.toUpperCase();
        } else if (activity && typeof activity === "object") {
          status = activity["imu-activity-received"]?.toUpperCase();
        }

        if (status === "MOVING" || status === "STOPPED" || status === "IDLE") {
          setImuStatus(status);
          console.log("Normalized IMU status:", status);
        } else {
          console.warn("Unknown IMU status:", status);
        }
      } catch (err) {
        console.error("Invalid IMU Activity", err);
      }
    });
  }, []);

  useEffect(() => {
    const handler = async (weight) => {
      const currentItem = weighingItemRef.current;
      if (!currentItem) {
        console.log("No weighing item selected yet, ignoring weight");
        return;
      }

      setCurrentWeight(weight);
      setCalculatedPrice((weight * currentItem.price).toFixed(2));

      const updatedItem = { ...currentItem, measuredWeight: weight };

      setTimeout(async () => {
        try {
          const result = await window.electronAPI.runPython("cart_ops.py", [
            "process_produce_upc",
            "UPC:" + updatedItem.upc,
            weight.toString(),
          ]);

          console.log("Result from Python:", result);

          if (result.status !== "success") {
            alert("Failed to add item to cart: " + result.message);
            return;
          }

          const product = result.product;

          setItems((prevItems) => {
            const existing = prevItems.find((i) => i.upc === product.upc);
            if (existing) {
              return prevItems.map((i) =>
                i.upc === product.upc ? { ...i, qty: (i.qty || 0) + weight } : i
              );
            } else {
              return [{ ...product }, ...prevItems];
            }
          });

          setShowWeighingPrompt(false);
          setWeighingItem(null);
          setCurrentWeight(0);
          setCalculatedPrice(0);
        } catch (err) {
          console.error(err);
          alert("Failed to add item to cart");
        }
      }, 2000);
    };

    window.electronAPI.onProduceWeightReceived(handler);

    return () => {
      window.electronAPI.removeProduceWeightReceived();
    };
  }, []);

  useEffect(() => {
    if (!showWeightStep) return;

    cancelWeighingRef.current = false;

    const pollIMU = async () => {
      try {
        let status;

        do {
          if (!showWeightStep) return;

          await window.electronAPI.sendStdinCommand("IMU_CHECK_ACTIVITY");

          await new Promise((res) => setTimeout(res, 1000));

          status = imuStatusRef.current; // get the latest status
          console.log("Polling IMU, current status:", status);
        } while (!cancelWeighingRef.current && status === "MOVING");

        if (cancelWeighingRef.current) return;

        console.log("Cart is stopped — measuring weight now");
        await new Promise((res) => setTimeout(res, 2000));
        await window.electronAPI.sendStdinCommand("MEASURE_PROD_WEIGHT");
      } catch (err) {
        console.error("Weighing error:", err);
      }
    };

    pollIMU();

    return () => {
      cancelWeighingRef.current = true;
    };
  }, [showWeightStep]);

  useEffect(() => {
    if (verificationStatus === "success") {
      setShowVerificationToast(false);
    } else {
      setShowVerificationToast(true);
    }
  }, [verificationStatus]);

  const totalItems = items.reduce((sum, i) => {
    const qty = Number(i.qty) || 0;
    return sum + (i.produce === 1 ? 1 : qty);
  }, 0);

  const totalPrice = items.reduce((sum, i) => {
    const price = Number(i.price) || 0;
    const qty = Number(i.qty) || 0;
    const weight = Number(i.weight) || 0;

    const subtotal = i.produce === 1 ? price * weight : price * qty;

    return sum + Math.max(0, subtotal);
  }, 0);

  function handleNotifyWorker(itemUPC) {
    // Light LED
    setShowWorkerHelp(true);
  }

  const openCheckoutModal = async () => {
    try {
      const unresolvedItems = await window.electronAPI.runPython(
        "cart_ops.py",
        ["get_unresolved_items"]
      );

      const hasUnresolved =
        unresolvedItems.status === "success" &&
        unresolvedItems.items.length > 0;

      const failedVerification = verificationStatus !== "success";

      if (failedVerification || hasUnresolved) {
        if (failedVerification) setShowVerificationBlock(true);
        if (hasUnresolved) setShowUnresolvedModal(true);
        return;
      }

      setShowCheckoutModal(true);
    } catch (err) {
      console.error("Error checking checkout requirements:", err);
    }
  };

  const handlePayment = async (method) => {
    try {
      setSelectedPayment(method);

      if (method === "QR Code") {
        // Send the full cart to the Python function
        const rawResult = await window.electronAPI.runPython("cart_ops.py", [
          "qr_payment",
          JSON.stringify(items), // <-- send items instead of totalPrice
        ]);

        console.log("Raw Python output:", rawResult);

        let result;
        try {
          result =
            typeof rawResult === "string" ? JSON.parse(rawResult) : rawResult;
        } catch (err) {
          console.error("Failed to parse Python response:", err, rawResult);
          alert("Internal error parsing response");
          return;
        }

        console.log("Parsed result:", result);

        if (!result || result.status !== "success" || !result.checkout_url) {
          alert(
            "Error generating payment link: " +
              (result.message || "Unknown error")
          );
          return;
        }

        setShowCheckoutModal(false);
        setPaymentUrl(result.checkout_url);
        setOrderId(result.order_id);
        setShowResultModal(true);

        console.log("QR payment order ID:", result.order_id);

        setTimeout(() => {
          pollPaymentStatus(result.order_id);
        }, 500);
      } else if (method === "Card") {
        setShowCheckoutModal(false);
        try {
          await window.electronAPI.sendStdinCommand("PAY_START");

          setPaymentMessage("💳 Waiting for card payment...");
          setShowPaymentModal(true);

          setTimeout(() => {
            setPaymentMessage(
              <div style={{ textAlign: "center" }}>
                <p>👉 Please tap your card on the reader now.</p>
                <img
                  src="/taptopay.png"
                  alt="Tap to pay"
                  style={{ width: "200px", marginTop: "15px" }}
                />
              </div>
            );
          }, 500);
        } catch (err) {
          console.error("Payment error:", err);
          setPaymentMessage("❌ Error processing payment");
        }
      }
    } catch (err) {
      console.error("Payment error:", err);
      alert("Error processing payment");
    }
  };

  // Poll payment status every 3s
  const pollPaymentStatus = (orderId) => {
    const interval = setInterval(async () => {
      try {
        const result = await window.electronAPI.runPython("cart_ops.py", [
          "get_order_status",
          orderId,
        ]);

        console.log("Polling order status:", result);

        if (result?.status === "COMPLETED" || result?.status === "APPROVED") {
          clearInterval(interval);

          setShowResultModal(false);
          setShowThankYouModal(true);
          setThankYouTime(new Date());
          try {
            const result = await window.electronAPI.runPython("cart_ops.py", [
              "end_session",
            ]);
            console.log("Python end session result:", result);

            await window.electronAPI.sendStdinCommand("CT_STOP");
          } catch (err) {
            console.error("Failed to clear backend cart via Python:", err);
          }
          // setTimeout(() => {
          //   navigate("/");
          // }, 8000);
        } else if (result?.status === "ERROR") {
          clearInterval(interval);
          alert("Payment error: " + (result.message || "Unknown"));
        }
        // else: still pending, do nothing
      } catch (err) {
        console.error("Polling failed:", err);
      }
    }, 2000);

    return () => clearInterval(interval);
  };

  useEffect(() => {
    const delayDebounce = setTimeout(async () => {
      if (!searchQuery.trim()) {
        setSearchResults([]);
        return;
      }

      try {
        const query = `
          SELECT p.upc, p.brand, p.description, p.price, p.weight, p.produce, p.qty,
                a.aisle, a.section, a.side, a.description AS locationDescription,
                i.url AS imageUrl,
                c.name AS category
          FROM Products p
          LEFT JOIN ProductCategories pc ON p.upc = pc.upc
          LEFT JOIN Categories c ON pc.category_id = c.id
          LEFT JOIN AisleLocations a ON p.upc = a.upc
          LEFT JOIN Images i ON p.upc = i.upc
          WHERE p.description LIKE '%${searchQuery}%'
            OR p.brand LIKE '%${searchQuery}%'
            OR p.upc LIKE '%${searchQuery}%'
            OR c.name LIKE '%${searchQuery}%'
          GROUP BY p.upc
          LIMIT 10;
        `;

        const result = await window.electronAPI.runPython("cart_ops.py", [
          "execute_sql",
          query,
        ]);
        console.log("Python search result:", result);

        setSearchResults(result.results || []);
      } catch (err) {
        console.error("Search error (Python):", err);
        setSearchResults([]);
      }
    }, 300);

    return () => clearTimeout(delayDebounce);
  }, [searchQuery]);

  const handleAdminLogin = () => {
    if (adminPassword === "eecs473") {
      setShowAdminLogin(false);
      setAdminPassword("");
      navigate("/admin");
    } else {
      alert("Incorrect password");
    }
  };

  return (
    <div className="cart-container">
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          alignItems: "center",
        }}
      >
        <h1>🛒 Cart</h1>
        <button
          className="btn"
          onClick={() => setShowAdminLogin(true)}
          style={{
            backgroundColor: "#6c757d",
            padding: "8px 16px",
          }}
        >
          🔐 Admin Mode
        </button>
      </div>

      <button
        style={{ backgroundColor: "white", color: "white" }}
        onClick={async () => {
          try {
            // const test_bright = await window.electronAPI.runPython("cart_ops.py", [
            //   "set_brightness",
            //   "0",
            // ]);
            // console.log(test_bright);

            const result = await window.electronAPI.runPython("cart_ops.py", [
              "process_upc",
              "123456789012",
            ]);

            console.log("Python result:", result);

            if (result.status !== "success") {
              alert("Python error: " + (result.message || "Unknown error"));
            }
          } catch (err) {
            console.error("Error running Python script:", err);
            alert("An exception occurred — check the console for details.");
          }
        }}
      >
        {/* Test UPC Scan */}
      </button>

      {/* Search bar */}
      <div
        style={{
          marginTop: "20px",
          marginBottom: "20px",
          position: "relative",
        }}
      >
        <input
          type="text"
          placeholder="Search for an item or add produce..."
          value={searchQuery}
          onChange={(e) => setSearchQuery(e.target.value)}
          style={{
            width: "100%",
            padding: "12px 40px 12px 15px",
            fontSize: "16px",
            borderRadius: "10px",
            border: "1px solid #ccc",
            boxShadow: "0 1px 3px rgba(0,0,0,0.1)",
          }}
        />
        {searchQuery && (
          <button
            onClick={() => setSearchQuery("")}
            style={{
              position: "absolute",
              right: "10px",
              top: "50%",
              transform: "translateY(-50%)",
              border: "none",
              background: "transparent",
              cursor: "pointer",
              fontSize: "16px",
              color: "#888",
            }}
          >
            ×
          </button>
        )}
      </div>

      {/* Search results table */}
      {searchResults.length > 0 && (
        <div
          style={{
            background: "#fff",
            borderRadius: "10px",
            padding: "15px",
            marginBottom: "25px",
            boxShadow: "0 2px 6px rgba(0,0,0,0.1)",
          }}
        >
          <h3 style={{ marginTop: 0 }}>Search Results</h3>
          <div style={{ overflowX: "auto" }}>
            <table
              style={{
                width: "100%",
                borderCollapse: "collapse",
                textAlign: "left",
              }}
            >
              <thead>
                <tr>
                  <th style={{ padding: "10px" }}>Image</th>
                  <th style={{ padding: "10px" }}>Description</th>
                  <th style={{ padding: "10px" }}>Qty</th>
                  <th style={{ padding: "10px" }}>Price</th>
                  <th style={{ padding: "10px" }}>Aisle - Section</th>
                  <th style={{ padding: "10px" }}>Action</th>
                </tr>
              </thead>
              <tbody>
                {searchResults.map((item) => (
                  <tr
                    key={item.upc}
                    style={{
                      borderTop: "1px solid #ddd",
                      verticalAlign: "middle",
                    }}
                  >
                    <td style={{ padding: "10px" }}>
                      <img
                        src={item.imageUrl || "https://via.placeholder.com/100"}
                        alt={item.description}
                        style={{
                          width: "80px",
                          height: "80px",
                          objectFit: "cover",
                          borderRadius: "6px",
                        }}
                      />
                    </td>

                    <td style={{ padding: "10px" }}>
                      {item.brand && (
                        <div style={{ fontSize: "0.9em", color: "#666" }}>
                          {item.brand}
                        </div>
                      )}
                      <strong>{item.description}</strong>
                      {/* Show quantity only if NOT a produce item */}
                      {item.produce !== 1 && (
                        <div
                          style={{
                            fontSize: "0.9em",
                            color: "#555",
                            marginTop: "4px",
                          }}
                        ></div>
                      )}
                    </td>

                    <td
                      style={{
                        padding: "10px",
                        textAlign: "left",
                        color: "#555",
                      }}
                    >
                      {item.produce !== 1 ? Number(item.qty ?? 0) : "-"}
                    </td>

                    <td style={{ padding: "10px" }}>
                      ${Number(item.price).toFixed(2)}
                    </td>

                    <td
                      style={{
                        padding: "10px",
                        fontSize: "0.9em",
                        color:
                          item.produce !== 1 && Number(item.qty ?? 0) === 0
                            ? "red"
                            : "#555",
                      }}
                    >
                      {(() => {
                        if (item.produce === 1) return "";
                        if (Number(item.qty ?? 0) === 0) return "Out of stock";
                        if (item.aisle)
                          return `${item.aisle}${
                            item.section ? " - " + item.section : ""
                          }`;
                        return "—";
                      })()}
                    </td>

                    <td style={{ padding: "10px" }}>
                      {item.produce === 1 ? (
                        <button
                          className="btn"
                          style={{ backgroundColor: "#28a745", color: "white" }}
                          onClick={() => {
                            setWeighingItem(item);
                            weighingItemRef.current = item;
                            setShowWeighingPrompt(true);

                            // Reset the step so modal starts at Step 1
                            setShowWeightStep(false);
                          }}
                        >
                          Weigh Item
                        </button>
                      ) : (
                        <span style={{ color: "#aaa" }}>—</span>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}

      <div className="items-table-wrapper">
        <table className="cart-table">
          <thead>
            <tr>
              <th>Item</th>
              <th>Unit Price</th>
              <th>Quantity</th>
              <th>Weight (Oz)</th>
              <th>Subtotal</th>
              <th>Action</th>
            </tr>
          </thead>
          <tbody>
            {items.length === 0 ? (
              <tr>
                <td colSpan="6">No items in cart</td>
              </tr>
            ) : (
              items.map((item) => (
                <tr key={item.upc}>
                  <td>{item.description}</td>

                  <td>${Number(item.price).toFixed(2)}</td>

                  <td>{item.produce === 1 ? "-" : item.qty}</td>

                  <td>
                    {item.produce === 1 ? Number(item.weight).toFixed(2) : "-"}
                  </td>

                  <td>
                    $
                    {item.produce === 1
                      ? (item.price * item.weight).toFixed(2)
                      : (item.price * item.qty).toFixed(2)}
                  </td>

                  <td
                    style={{
                      display: "flex",
                      gap: "0.5rem",
                      alignItems: "center",
                    }}
                  >
                    {item.requires_verification && !item.resolved ? (
                      <button
                        className="btn notify"
                        onClick={() => handleNotifyWorker(item.upc)}
                      >
                        Needs Attention, Click to notify Worker
                      </button>
                    ) : null}

                    <button
                      className="btn remove"
                      onClick={() => {
                        setItemToRemove(item);
                        if (item.produce === 1) {
                          setRemoveQty(Number(item.weight));
                        } else {
                          setRemoveQty(1);
                        }
                        setShowRemoveConfirmModal(true);
                      }}
                    >
                      x
                    </button>
                  </td>
                </tr>
              ))
            )}
          </tbody>
        </table>
      </div>

      <div className="totals">
        <div className="totals-info">
          <p>
            <strong>Total Items:</strong> {Math.max(0, totalItems)}
          </p>
          <p>
            <strong>Total Price:</strong> ${totalPrice.toFixed(2)}
          </p>
        </div>

        <button
          onClick={() => {
            if (helpRequested) {
              // Cancel help
              setHelpRequested(false);
              setShowWorkerHelp(false);
            } else {
              // Ask for help
              setHelpRequested(true);
              setShowWorkerHelp(true);
            }
          }}
          style={{
            padding: "0.5rem 1rem",
            fontSize: "1rem",
            cursor: "pointer",
            borderRadius: "8px",
            backgroundColor: helpRequested ? "#dc3545" : "#8b5f18ff",
            color: "white",
            border: "none",
          }}
        >
          {helpRequested ? "Cancel Help" : "Ping an Employee"}
        </button>

        <div className="actions">
          <button className="btn checkout" onClick={openCheckoutModal}>
            Checkout →
          </button>
        </div>
      </div>

      {/* Admin Login Modal */}
      {showAdminLogin && (
        <div className="modal-overlay">
          <div className="modal">
            <h2>Admin Login</h2>
            <p>Enter admin password to access database management</p>
            <input
              type="password"
              value={adminPassword}
              onChange={(e) => setAdminPassword(e.target.value)}
              onKeyPress={(e) => e.key === "Enter" && handleAdminLogin()}
              placeholder="Password"
              style={{
                width: "100%",
                padding: "10px",
                fontSize: "16px",
                marginBottom: "15px",
              }}
              autoFocus
            />
            <div className="modal-actions">
              <button className="btn" onClick={handleAdminLogin}>
                Login
              </button>
              <button
                className="btn cancel-btn"
                onClick={() => {
                  setShowAdminLogin(false);
                  setAdminPassword("");
                }}
              >
                Cancel
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Checkout Modal */}
      {showCheckoutModal && (
        <div className="modal-overlay">
          <div className="modal">
            <h2>Choose Payment Method</h2>
            <div className="modal-actions">
              <button className="btn" onClick={() => handlePayment("QR Code")}>
                QR Code
              </button>
              <button className="btn" onClick={() => handlePayment("Card")}>
                Card
              </button>
              <button
                className="btn cancel-btn"
                onClick={() => setShowCheckoutModal(false)}
              >
                Cancel
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Weighing Modal */}
      {showWeighingPrompt && weighingItem && (
        <div
          className="weighing-modal"
          style={{
            position: "fixed",
            top: 0,
            left: 0,
            right: 0,
            bottom: 0,
            background: "rgba(0,0,0,0.5)",
            display: "flex",
            justifyContent: "center",
            alignItems: "center",
            zIndex: 1000,
          }}
        >
          <div
            style={{
              background: "#fff",
              padding: "20px",
              borderRadius: "10px",
              width: "300px",
              textAlign: "center",
            }}
          >
            {!showWeightStep ? (
              // Step 1: Place item on scale
              <>
                <h3>
                  Place item on scale. Please stay still for the most accurate
                  reading.
                </h3>
                <button
                  style={{ marginTop: "10px" }}
                  onClick={() => {
                    setShowWeightStep(true); // Move to step 2
                    // Optionally start the weighing process here
                  }}
                >
                  OK
                </button>
                <button
                  style={{ marginTop: "10px", marginLeft: "10px" }}
                  onClick={() => {
                    // Cancel
                    cancelWeighingRef.current = true;
                    setShowWeighingPrompt(false);
                    setWeighingItem(null);
                    setCurrentWeight(0);
                    setCalculatedPrice(0);
                    setShowWeightStep(false);
                  }}
                >
                  Cancel
                </button>
              </>
            ) : (
              // Step 2: Show weighing status
              <>
                {imuStatus === "MOVING" ? (
                  <p style={{ color: "red" }}>
                    Cart is moving — please stay still.
                  </p>
                ) : currentWeight > 0 ? (
                  <>
                    <p>Weight: {currentWeight.toFixed(2)} oz</p>
                    <p>Price: ${calculatedPrice}</p>
                  </>
                ) : (
                  <p>Waiting for weight...</p>
                )}
                <button
                  style={{ marginTop: "10px" }}
                  onClick={() => {
                    // Cancel weighing
                    cancelWeighingRef.current = true;
                    setShowWeighingPrompt(false);
                    setWeighingItem(null);
                    setCurrentWeight(0);
                    setCalculatedPrice(0);
                  }}
                >
                  Cancel
                </button>
              </>
            )}
          </div>
        </div>
      )}

      {/* Result Modal */}
      {showResultModal && (
        <div className="modal-overlay">
          <div
            className="modal"
            style={{ textAlign: "center", minWidth: "400px" }}
          >
            <h2>{selectedPayment} Payment</h2>
            <p>
              <strong>Total Price:</strong> ${totalPrice.toFixed(2)}
            </p>
            {selectedPayment === "QR Code" ? (
              <div>
                <p>Scan this QR code to pay:</p>
                <QRCodeCanvas value={paymentUrl} size={200} />
                <p style={{ marginTop: "10px", wordBreak: "break-all" }}>
                  Or click the link:{" "}
                  <a
                    href={paymentUrl}
                    target="_blank"
                    rel="noopener noreferrer"
                  >
                    {paymentUrl}
                  </a>
                </p>
              </div>
            ) : (
              <p>
                You selected <strong>{selectedPayment}</strong> payment method
              </p>
            )}
            <div className="modal-actions" style={{ marginTop: "20px" }}>
              <button className="btn" onClick={() => setShowResultModal(false)}>
                Back
              </button>
            </div>
          </div>
        </div>
      )}

      {/* RemoveConfirm Modal */}
      {showRemoveConfirmModal && itemToRemove && (
        <div className="modal-overlay">
          <div className="modal">
            <h2>Remove Item</h2>
            <p>
              Remove <strong>{itemToRemove.description}</strong> from the cart
            </p>
            <p>Available quantity: {Math.abs(itemToRemove.qty)}</p>

            <div
              style={{
                margin: "20px 0",
                display: "flex",
                alignItems: "center",
                justifyContent: "center",
                gap: "10px",
              }}
            >
              <label>
                <strong>Quantity to remove:</strong>
              </label>
              <input
                type="number"
                min="1"
                max={itemToRemove.qty} // <--- if null, this is bad
                value={removeQty}
                onChange={(e) =>
                  setRemoveQty(
                    Math.min(
                      itemToRemove.qty,
                      Math.max(1, parseInt(e.target.value) || 1)
                    )
                  )
                }
              />
            </div>

            <div className="modal-actions">
              <button
                className="btn"
                onClick={async () => {
                  try {
                    const result = await window.electronAPI.runPython(
                      "cart_ops.py",
                      [
                        "update_cart",
                        JSON.stringify({
                          upc: itemToRemove.upc,
                          remove_qty: removeQty,
                        }),
                      ]
                    );

                    console.log("Python update_cart result:", result);

                    if (removeQty >= itemToRemove.qty) {
                      setItems(items.filter((i) => i.upc !== itemToRemove.upc));
                    } else {
                      setItems(
                        items.map((i) =>
                          i.upc === itemToRemove.upc
                            ? { ...i, qty: i.qty - removeQty }
                            : i
                        )
                      );
                    }
                  } catch (err) {
                    console.error(
                      "Failed to update inventory via Python:",
                      err
                    );
                    alert("Error updating inventory");
                  }

                  setShowRemoveConfirmModal(false);
                  setItemToRemove(null);
                  setRemoveQty(1);
                }}
              >
                Remove {removeQty === itemToRemove.qty ? "All" : removeQty}
              </button>

              <button
                className="btn cancel-btn"
                onClick={() => {
                  setShowRemoveConfirmModal(false);
                  setItemToRemove(null);
                  setRemoveQty(1);
                }}
              >
                Cancel
              </button>
            </div>
          </div>
        </div>
      )}

      {showUnresolvedModal && (
        <div className="modal-overlay">
          <div className="modal">
            <h2>⚠️ Items Require ID Check</h2>
            <p>
              Some items in your cart require ID check before checkout. You must
              wait for a worker before you checkout.
            </p>
            <button
              className="btn"
              onClick={() => {
                setShowUnresolvedModal(false);
                setShowWorkerHelp(true);
              }}
            >
              OK
            </button>
          </div>
        </div>
      )}

      {showWorkerHelp && (
        <div
          style={{
            position: "fixed",
            bottom: "20px",
            right: "20px",
            backgroundColor: "#ffc107",
            color: "#000",
            padding: "12px 20px",
            borderRadius: "8px",
            boxShadow: "0 2px 6px rgba(0,0,0,0.2)",
            zIndex: 9999,
            minWidth: "200px",
            fontSize: "14px",
          }}
        >
          <strong>Worker Notified</strong>
        </div>
      )}

      {showVerificationToast && (
        <div
          style={{
            position: "fixed",
            bottom: "20px",
            left: "20px",
            backgroundColor: "#f8d7da",
            color: "#721c24",
            padding: "12px 20px",
            borderRadius: "8px",
            boxShadow: "0 2px 6px rgba(0,0,0,0.2)",
            zIndex: 1000,
            minWidth: "250px",
            fontSize: "14px",
          }}
        >
          ⚠️ Weight or RFID mismatch detected. Please notify a worker.
        </div>
      )}

      {showVerificationBlock && (
        <div className="modal-overlay">
          <div className="modal">
            <h2>Issue with items in your cart.</h2>
            <p>
              Some items in your cart failed weight or RFID verification. You
              must wait for a worker before you checkout.
            </p>
            <button
              className="btn"
              onClick={() => {
                setShowVerificationBlock(false);
                setShowWorkerHelp(true);
              }}
              style={{ marginTop: "15px" }}
            >
              OK
            </button>
          </div>
        </div>
      )}

      {showEmailModal && (
        <div className="modal-overlay">
          <div className="modal">
            <h3>Send Receipt?</h3>
            <p>Enter your email if you'd like us to email your receipt.</p>

            <input
              type="email"
              value={receiptEmail}
              onChange={(e) => setReceiptEmail(e.target.value)}
              placeholder="Email address (optional)"
            />

            <button
              onClick={async () => {
                if (receiptEmail.trim() !== "") {
                  const orderDetails = {
                    email: receiptEmail,
                    orderId: orderId,
                    items: items
                      .filter(
                        (i) =>
                          i != null && i.description != null && i.price != null
                      )
                      .map((i) => ({
                        name: i.description,
                        quantity: i.produce === 1 ? i.weight : i.qty,
                        price: i.price,
                      })),

                    total: items.reduce((sum, i) => {
                      const qty = i.produce === 1 ? i.weight : i.qty;
                      return sum + i.price * qty;
                    }, 0),
                    orderId: orderId,
                  };

                  await window.electronAPI.runPython("cart_ops.py", [
                    "send_receipt",
                    JSON.stringify(orderDetails),
                  ]);
                  // console.log("Receipt result:", result);
                }

                setShowEmailModal(false); // close the modal
                setItems([]);
                navigate("/");
              }}
            >
              Send Receipt
            </button>
          </div>
        </div>
      )}

      {showThankYouModal && (
        <div className="modal-overlay">
          <div
            className="modal"
            style={{ textAlign: "center", minWidth: "400px" }}
          >
            <h2>Thank you for shopping! 🎉</h2>
            <p>Your payment was received successfully.</p>
            <div style={{ marginTop: "20px", fontSize: "16px" }}>
              <p>
                <strong>Total Paid:</strong> ${totalPrice.toFixed(2)}
              </p>
              {thankYouTime && (
                <p>
                  <strong>Date & Time:</strong> {thankYouTime.toLocaleString()}
                </p>
              )}
            </div>

            <div
              style={{
                display: "flex",
                gap: "10px",
                justifyContent: "center",
                marginTop: "20px",
              }}
            >
              <button
                className="btn"
                onClick={() => {
                  setShowThankYouModal(false);
                  setShowEmailModal(true);
                }}
              >
                Email Receipt
              </button>
              <button
                className="btn"
                onClick={() => {
                  setShowThankYouModal(false);
                  setItems([]);
                  setItems([]);
                  navigate("/");
                }}
              >
                New Shopper
              </button>
            </div>
          </div>
        </div>
      )}

      {showPaymentModal && (
        <div className="payment-modal">
          <div className="payment-modal-content">
            <p>{paymentMessage}</p>

            <button
              style={{
                marginTop: "15px",
                padding: "10px 20px",
                border: "none",
                borderRadius: "5px",
                backgroundColor: "#dc3545",
                color: "white",
                cursor: "pointer",
              }}
              onClick={() => {
                setShowPaymentModal(false);
                setPaymentMessage("");
              }}
            >
              Cancel
            </button>
          </div>
        </div>
      )}
      <img
        src="/cart.png"
        alt="Cart Icon"
        style={{
          position: "absolute",
          bottom: 10,
          left: 10,
          width: "50px",
          height: "auto",
          zIndex: 10,
        }}
      />
      <img
        src="/M.png"
        alt="Cart Icon"
        style={{
          position: "absolute",
          top: 10,
          right: 10,
          width: "50px",
          height: "auto",
          zIndex: 10,
        }}
      />
    </div>
  );
};

export default Cart;
