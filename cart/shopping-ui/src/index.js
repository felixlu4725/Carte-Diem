import { StrictMode, useEffect, useState } from "react";
import { createRoot } from "react-dom/client";
import { BrowserRouter, Routes, Route, useNavigate } from "react-router-dom";
import Cart from "./Cart.jsx";
import Admin from "./Admin.jsx";
import Setup from "./Setup.jsx";
import "./index.css";
import reportWebVitals from './reportWebVitals';

const PlaceItemsScreen = ({ onNext }) => {
  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        textAlign: 'center',
        gap: '20px',         
      }}
    >
      <h2>Please place your belongings into the cart.</h2>
      <img 
        src="/incart.png" 
        alt="Items on cart" 
        style={{ 
          maxWidth: '50%',   
          height: 'auto',  
        }} 
      />
      <button className="btn" onClick={onNext}>
        Continue
      </button>
    </div>
  );
};

// Landing screen for customer
const Landing = () => {
  const navigate = useNavigate();
  const [showPlaceItems, setShowPlaceItems] = useState(false);

  const startSession = async () => {
    try {
      const result = await window.electronAPI.runPython("cart_ops.py", ["start_session"]);
      console.log("Python start_session result:", result);
      // await window.electronAPI.sendStdinCommand("TARE_PRODUCE_WEIGHT");
      // await window.electronAPI.sendStdinCommand("TARE_CART_WEIGHT");
      await window.electronAPI.sendStdinCommand("CT_START");
      navigate("/cart");
    } catch (error) {
      console.error("Python error:", error);
      console.error("Error sending stdin command:", error);
      alert("Error starting session via Python");
    }
  };

  const handleStartShopping = async () => {
    try {
      setShowPlaceItems(true);
    } catch (error) {
      alert("Failed to initialize cart");
    }
  };

  if (showPlaceItems) {
    return <PlaceItemsScreen onNext={startSession} />;
  }

  return (
    <div className="landing-screen" style={{ position: "relative", padding: "20px" }}>
      <img
  src="/M.png"
  alt="Logo"
  style={{
    position: "absolute",
    top: 10,
    right: 10,
    width: 50,
    height: "auto",
    marginRight: '-300px',
    marginTop: '20px',
  }}
/>

<img
  src="/cart.png"
  alt="Cart"
  style={{
    position: "absolute",
    bottom: 10,
    left: 10,
    width: 50,
    height: "auto",
    marginLeft: '-300px',
    marginBottom: '20px',
  }}
/>

      <h1>Welcome to the EECS473 Store!</h1>
      <h2>Powered by Carte Diem!</h2>
      <h3></h3>
      <button className="btn" onClick={handleStartShopping}>
        Start Shopping
      </button>
    </div>
  );
};

function SetupOrLanding() {
  const [setupDone, setSetupDone] = useState(null);

  useEffect(() => {
    const checkSetup = async () => {
      try {
        const result = await window.electronAPI.runPython("cart_ops.py", ["get_setup"]);

        if (result.status === "success") {
          setSetupDone(result.setup_complete === 1);
        } else {
          console.error("Failed to fetch setup:", result);
          setSetupDone(false);
        }
      } catch (error) {
        console.error("Error checking setup:", error);
        setSetupDone(false);
      }
    };

    checkSetup();
  }, []);

  if (setupDone === null) return <div>Loading...</div>;

  return setupDone ? <Landing /> : <Setup onGoToCart={() => setSetupDone(true)} />;
}

function App() {
  const [items, setItems] = useState([]);
  const [showWorkerHelp, setShowWorkerHelp] = useState(false);
  const [verificationStatus, setVerificationStatus] = useState("success");
  const [verificationInfo, setVerificationInfo] = useState({
      measuredWeight: 0,
      expectedWeight: 0,
      weightDifference: 0,
      scannedTags: [],
      rfidUpcs: [],
    });

  useEffect(() => {
    const removeListener = window.electronAPI.onItemVerificationReceived(
      (verification) => {
        console.log("Verification:", verification);

        setVerificationStatus(verification.status);

        setVerificationInfo({
          measuredWeight: verification.measured_weight,
          expectedWeight: verification.expected_weight,
          weightDifference: verification.weight_difference,
          scannedTags: verification.tags_scanned,
          rfidUpcs: verification.rfid_upcs,
        });
      }
    );

    return () => {
      // If your preload returns an unsubscribe function, call it here
      if (removeListener) removeListener();
    };
  }, []);

  return (
    <BrowserRouter>
      <Routes>
        <Route path="/" element={<SetupOrLanding />} />
          <Route
            path="/cart"
            element={
              <Cart
                items={items}
                setItems={setItems}
                showWorkerHelp={showWorkerHelp}
                setShowWorkerHelp={setShowWorkerHelp}
                verificationStatus={verificationStatus}
                setVerificationStatus={setVerificationStatus}
                verificationInfo={verificationInfo}
                setVerificationInfo={setVerificationInfo}
              />
            }
          />

          <Route
            path="/admin"
            element={
              <Admin
                items={items}
                setItems={setItems}
                showWorkerHelp={showWorkerHelp}
                setShowWorkerHelp={setShowWorkerHelp}
                verificationStatus={verificationStatus}
                setVerificationStatus={setVerificationStatus}
                verificationInfo={verificationInfo}
                setVerificationInfo={setVerificationInfo}
              />
            }
          />
      </Routes>
    </BrowserRouter>
  );
}

createRoot(document.getElementById("root")).render(
  <StrictMode>
    <App />
  </StrictMode>
);

reportWebVitals();
