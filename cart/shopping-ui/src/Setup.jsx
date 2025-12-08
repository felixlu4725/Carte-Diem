import React, { useState, useEffect } from "react";
import "./Setup.css";

function Setup({ onGoToCart }) {
  const [masterIp, setMasterIp] = useState("");
  const [masterPort, setMasterPort] = useState("");
  const [companyIp, setCompanyIp] = useState("");
  const [companyPort, setCompanyPort] = useState("");
  const [isSetupDone, setIsSetupDone] = useState(false);

  useEffect(() => {
    const fetchSetup = async () => {
      try {
        const result = await window.electronAPI.runPython("cart_ops.py", ["get_setup"]);

        if (result.status === "success") {
          setMasterIp(result.master_ip || "");
          setMasterPort(result.master_port || "");
          setCompanyIp(result.company_ip || "");
          setCompanyPort(result.company_port || "");
          setIsSetupDone(result.setup_complete === 1);
        } else {
          console.error("Failed to fetch setup:", result);
        }
      } catch (err) {
        console.error("Error fetching setup:", err);
      }
    };

    fetchSetup();
  }, []);

  const handleSubmit = async (e) => {
    e.preventDefault();

    if (!masterIp.trim() || !masterPort.trim() || !companyIp.trim() || !companyPort.trim()) {
      alert("Please fill in all fields.");
      return;
    }

    try {
      const result = await window.electronAPI.runPython("cart_ops.py", [
        "save_setup",
        masterIp.trim(),
        masterPort.trim(),
        companyIp.trim(),
        companyPort.trim(),
      ]);

      if (result.status === "success") {
        setIsSetupDone(true);
      } else {
        alert("Failed to save setup. Check console for details.");
        console.error("Save setup error:", result);
      }
    } catch (err) {
      console.error("Error saving setup:", err);
      alert("Failed to save setup. Check console for details.");
    }
  };

  return (
    <div className="setup-container">
      {!isSetupDone ? (
        <>
          <h1>Initial Setup</h1>
          <form onSubmit={handleSubmit}>
            <div className="setup-form-group">
              <label>Master Server IP:</label>
              <input
                type="text"
                value={masterIp}
                onChange={(e) => setMasterIp(e.target.value)}
              />
            </div>

            <div className="setup-form-group">
              <label>Master Server Port:</label>
              <input
                type="text"
                value={masterPort}
                onChange={(e) => setMasterPort(e.target.value)}
              />
            </div>

            <div className="setup-form-group">
              <label>Company IP (Carte Diem):</label>
              <input
                type="text"
                value={companyIp}
                onChange={(e) => setCompanyIp(e.target.value)}
              />
            </div>

            <div className="setup-form-group">
              <label>Company Port (Carte Diem):</label>
              <input
                type="text"
                value={companyPort}
                onChange={(e) => setCompanyPort(e.target.value)}
              />
            </div>

            <button type="submit" className="setup-btn">
              Save
            </button>
          </form>
        </>
      ) : (
        <div className="setup-complete">
          <h1>Setup Complete</h1>
          <p>Master server: {masterIp}:{masterPort}</p>
          <p>Company server: {companyIp}:{companyPort}</p>
          <button className="setup-btn" onClick={onGoToCart}>
            Go to Cart Interface
          </button>
        </div>
      )}
    </div>
  );
}

export default Setup;
