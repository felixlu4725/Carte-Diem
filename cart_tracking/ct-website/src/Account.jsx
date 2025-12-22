import { useState } from "react";
import "./Account.css";

function Account({ action, goBack, goToDashboard }) {
  const [address, setAddress] = useState("");
  const [storeKey, setStoreKey] = useState("");
  const [storeName, setStoreName] = useState("");
  const [storePort, setStorePort] = useState("");
  const [serverUrl, setServerUrl] = useState("");
  const [accountCreated, setAccountCreated] = useState(false);
  const [loginID, setLoginID] = useState("");
  const [isCreating, setIsCreating] = useState(false);
  const [isLoggingIn, setIsLoggingIn] = useState(false);

  // Create account
  const handleCreate = async (e) => {
    e.preventDefault();
    setIsCreating(true);
    try {
      const res = await fetch("http://localhost:5100/register_store", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ store_name: storeName, store_address: address }),
      });
      const data = await res.json();

      if (data.success) {
        setStoreKey(data.store_key);
        setStoreName(data.store_name);
        setStorePort(data.port);
        setServerUrl(data.server_url);
        setAccountCreated(true);
      } else {
        alert(data.error);
      }
    } catch (err) {
      console.error("Registration error:", err);
      alert("Something went wrong. Please try again.");
    } finally {
      setIsCreating(false);
    }
  };

  // Login
  const handleLogin = async (e) => {
    e.preventDefault();
    setIsLoggingIn(true);
    try {
      const res = await fetch("http://localhost:5100/login_store", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ store_key: loginID }),
      });
      const data = await res.json();

      if (data.success) {
        const port = new URL(data.redirect_url).port;

        console.log("Login successful!");
        console.log("Redirect URL:", data.redirect_url);
        console.log("Port:", port);
        
        setStoreKey(data.store_key);
        setStoreName(data.store_name);
        setStorePort(data.port);
        setServerUrl(data.server_url);

        goToDashboard(data.redirect_url);
      } else {
        alert(data.error);
      }
    } catch (err) {
      console.error("Login error:", err);
      alert("Something went wrong. Please try again.");
    } finally {
      setIsLoggingIn(false);
    }
  };

  return (
    <div className="account-container">
      <h1 className="main-title">Cart Tracking System</h1>

      {/* Create Account */}
      {action === "create" && (
        <div className="account-card">
          <h2 className="account-title">Create Account</h2>
          {!accountCreated ? (
            <form className="account-form" onSubmit={handleCreate}>
              <div className="form-group">
                <label className="form-label">Store Name:</label>
                <input
                  className="form-input"
                  value={storeName}
                  onChange={(e) => setStoreName(e.target.value)}
                  required
                  disabled={isCreating}
                />
              </div>
              <div className="form-group">
                <label className="form-label">Address:</label>
                <input
                  className="form-input"
                  value={address}
                  onChange={(e) => setAddress(e.target.value)}
                  required
                  disabled={isCreating}
                />
              </div>
              <button className="primary-btn" type="submit" disabled={isCreating}>
                {isCreating ? "Generating Store ID..." : "Create Account"}
              </button>
            </form>
          ) : (
            <div className="success-message">
              <p>Account created successfully! 🎉</p>
              <p>
                Your Store ID: <strong>{storeKey}</strong>
              </p>
              <p className="hint-text">
                Save this ID — you'll need it to log in!
              </p>
              <button className="primary-btn" onClick={() => goToDashboard(serverUrl)}>
                Go to Dashboard
              </button>
            </div>
          )}
        </div>
      )}

      {/* Login */}
      {action === "login" && (
        <div className="account-card">
          <h2 className="account-title">Login</h2>
          <form className="account-form" onSubmit={handleLogin}>
            <div className="form-group">
              <label className="form-label">Store ID:</label>
              <input
                className="form-input"
                value={loginID}
                onChange={(e) => setLoginID(e.target.value)}
                required
                placeholder="Enter your Store ID"
                disabled={isLoggingIn}
              />
            </div>
            <button className="primary-btn" type="submit" disabled={isLoggingIn}>
              {isLoggingIn ? "Logging in..." : "Login"}
            </button>
          </form>
        </div>
      )}

      {/* Go Back */}
      <button className="secondary-btn" onClick={goBack} disabled={isCreating || isLoggingIn}>
        Back
      </button>
    </div>
  );
}

export default Account;