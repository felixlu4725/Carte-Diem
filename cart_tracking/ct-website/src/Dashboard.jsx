import { useState, useEffect } from "react";
import { FaHome, FaMapMarkedAlt, FaDatabase, FaPuzzlePiece, FaBars, FaSignOutAlt, FaShoppingCart } from "react-icons/fa";
import HomeTab from "./tabs/HomeTab";
import MapTab from "./tabs/MapTab";
import CartsTab from "./tabs/CartsTab";
import DataTab from "./tabs/DataTab";
import StoreBoundaryTab from './tabs/StoreBoundaryTab';
import "./Dashboard.css";

function Dashboard({ serverUrl, onLogout }) {
  const [activeTab, setActiveTab] = useState("HOME");
  const [originPort, setOriginPort] = useState(null);
  const [sidebarOpen, setSidebarOpen] = useState(true);
  const [storeData, setStoreData] = useState(null);
  const [loading, setLoading] = useState(true);
  
  useEffect(() => {
    const fetchStoreInfo = async () => {
        setLoading(true);
        try {
            const url = new URL(serverUrl);
            url.hostname = "35.3.245.73";
            const res = await fetch(`${url.origin}/store/info`);
            const data = await res.json();

          if (data.success) {
              setStoreData({
              store_name: data.store_name,
              store_key: data.store_key,
              server_port: data.port
              });
          } else {
              console.warn("Failed to fetch store info, using defaults");
              setStoreData({ store_name: "Unknown Store", store_key: "", server_port: "" });
          }
        } catch (err) {
        console.error("Error fetching store info:", err);
        setStoreData({ store_name: "Unknown Store", store_key: "", server_port: "" });
        } finally {
        setLoading(false);
        }
    };

    fetchStoreInfo();
    }, [serverUrl]);

  useEffect(() => {
    const params = new URLSearchParams(window.location.search);
    const port = params.get("originPort");
    if (port) setOriginPort(port);
  }, []);

  if (loading) return <p>Loading store parameters...</p>;

  const tabs = [
    { name: "HOME", icon: <FaHome /> },
    { name: "MAP", icon: <FaMapMarkedAlt /> },
    { name: "CARTS", icon: <FaShoppingCart /> },
    { name: "DATA", icon: <FaDatabase /> },
    { name: "STORE BOUNDARY", icon: <FaPuzzlePiece /> },
  ];

  const renderContent = () => {
    if (!storeData) return <p>Loading store information...</p>;

    switch (activeTab) {
      case "HOME":
        return <HomeTab storeData={storeData} />;
      case "MAP":
        return <MapTab storeData={storeData} />;
      case "CARTS":
        return <CartsTab storeData={storeData} />;
      case "DATA":
        return <DataTab storeData={storeData} />;
      case "STORE BOUNDARY":
        return <StoreBoundaryTab storeData={storeData} />;
      default:
        return <p>Select a tab.</p>;
    }
  };

  const handleLogout = () => {
    if (onLogout) {
      onLogout();
    } else {
      const backPort = originPort;
      window.location.href = `http://localhost:${backPort}`;
    }
  };

  return (
    <div className="dashboard-container">
      <div className={`sidebar ${sidebarOpen ? "" : "closed"}`}>
        <div className="sidebar-top">
          <button className="toggle-btn" onClick={() => setSidebarOpen(!sidebarOpen)}>
            <FaBars />
          </button>
        </div>

        <div className="tabs">
          {tabs.map(tab => (
            <div
              key={tab.name}
              className={`sidebar-tab ${activeTab === tab.name ? "active" : ""}`}
              onClick={() => setActiveTab(tab.name)}
            >
              <span className="tab-icon">{tab.icon}</span>
              <span className="tab-text">{tab.name}</span>
            </div>
          ))}
        </div>

        <button
          className="logout-btn"
          onClick={handleLogout}
        >
          <FaSignOutAlt /> {sidebarOpen && "Log Out"}
        </button>

        <div className="store-id">
          {sidebarOpen && `Store: ${storeData?.store_key || "Loading..."}`}
        </div>
      </div>

      <div className="main-content">{renderContent()}</div>
    </div>
  );
}

export default Dashboard;