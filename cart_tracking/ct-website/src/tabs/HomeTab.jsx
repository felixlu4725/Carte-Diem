import { useEffect, useState, useMemo } from "react";
import "./HomeTab.css";

const formatDateForAPI = (dateStr) => {
  const [year, month, day] = dateStr.split("-");
  return `${month}_${day}_${year.slice(-2)}`;
};

const getTodayInputValue = () => {
  const today = new Date();
  return today.toISOString().split("T")[0];
};

function HomeTab({ storeData }) {
  const serverUrl = useMemo(
    () => `http://35.3.245.73:${storeData?.server_port}`,
    [storeData?.server_port]
  );

  const [selectedDate, setSelectedDate] = useState(getTodayInputValue());
  const [selectedCart, setSelectedCart] = useState("");
  const [selectedSession, setSelectedSession] = useState("");

  const [availableCarts, setAvailableCarts] = useState([]);
  const [availableSessions, setAvailableSessions] = useState([]);

  const [appliedDate, setAppliedDate] = useState(getTodayInputValue());
  const [appliedCart, setAppliedCart] = useState("");
  const [appliedSession, setAppliedSession] = useState("");

  const [imagePairs, setImagePairs] = useState([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);

  const [modalImage, setModalImage] = useState(null);
  const [compareSelections, setCompareSelections] = useState([]);

  useEffect(() => {
    if (!selectedDate) return;

    const fetchCarts = async () => {
      try {
        const res = await fetch(
          `${serverUrl}/images/list?date=${formatDateForAPI(selectedDate)}`
        );
        if (!res.ok) return;

        const data = await res.json();
        const dateKey = formatDateForAPI(selectedDate);
        const carts = Object.keys(data.images?.[dateKey] || {});
        setAvailableCarts(carts);
      } catch {
        setAvailableCarts([]);
      }
    };

    setSelectedCart("");
    setSelectedSession("");
    setAvailableSessions([]);
    fetchCarts();
  }, [selectedDate, serverUrl]);

  useEffect(() => {
    if (!selectedCart || !selectedDate) return;

    const fetchSessions = async () => {
      try {
        const res = await fetch(
          `${serverUrl}/images/list?date=${formatDateForAPI(selectedDate)}&cart_id=${selectedCart}`
        );
        if (!res.ok) return;

        const data = await res.json();
        const dateKey = formatDateForAPI(selectedDate);
        const cartData = data.images?.[dateKey]?.[selectedCart] || {};

        const heatmaps = cartData.heatmaps || [];
        const paths = cartData.paths || [];

        const sessions = Array.from(
          new Set([
            ...heatmaps.map((f) => f.replace("cart_heat_map_", "").replace(".png", "")),
            ...paths.map((f) => f.replace("raw_path_points_", "").replace(".png", "")),
          ])
        );

        setAvailableSessions(sessions);
      } catch {
        setAvailableSessions([]);
      }
    };

    setSelectedSession("");
    fetchSessions();
  }, [selectedCart, selectedDate, serverUrl]);

  useEffect(() => {
    if (!appliedDate) return;

    const fetchImages = async () => {
      setLoading(true);
      setError(null);
      setImagePairs([]);

      try {
        const dateKey = formatDateForAPI(appliedDate);

        const url = new URL(`${serverUrl}/images/list`);
        url.searchParams.append("date", dateKey);
        if (appliedCart) url.searchParams.append("cart_id", appliedCart);

        const res = await fetch(url.toString());
        if (!res.ok) throw new Error("Failed to fetch images");

        const data = await res.json();
        let pairs = [];

        const carts = appliedCart ? [appliedCart] : Object.keys(data.images?.[dateKey] || {});

        for (const cart of carts) {
          const cartData = data.images?.[dateKey]?.[cart] || {};
          const heatmaps = cartData.heatmaps || [];
          const paths = cartData.paths || [];

          const sessionMap = {};

          heatmaps.forEach((file) => {
            const session = file.replace("cart_heat_map_", "").replace(".png", "");
            if (appliedSession && session !== appliedSession) return;
            if (!sessionMap[session]) sessionMap[session] = {};
            sessionMap[session].heatmap = `${serverUrl}/images/${dateKey}/${cart}/${file}`;
          });

          paths.forEach((file) => {
            const session = file.replace("raw_path_points_", "").replace(".png", "");
            if (appliedSession && session !== appliedSession) return;
            if (!sessionMap[session]) sessionMap[session] = {};
            sessionMap[session].path = `${serverUrl}/images/${dateKey}/${cart}/${file}`;
          });

          Object.entries(sessionMap).forEach(([session, imgs]) => {
            pairs.push({
              date: appliedDate,
              session,
              cart,
              heatmap: imgs.heatmap || null,
              path: imgs.path || null,
            });
          });
        }

        setImagePairs(pairs);
      } catch (err) {
        console.error(err);
        setError("No images found for selected filters.");
      } finally {
        setLoading(false);
      }
    };

    fetchImages();
  }, [appliedDate, appliedCart, appliedSession, serverUrl]);

  const confirmFilters = () => {
    setAppliedDate(selectedDate);
    setAppliedCart(selectedCart);
    setAppliedSession(selectedSession);
  };

  return (
    <div className="home-tab-container">
      <h1>Welcome, {storeData.store_name}!</h1>
      {/* HEADER */}
      <div className="header">
        <h2>Generated Data Images</h2>
        <button
          className="btn-primary"
          onClick={async () => {
            try {
              const res = await fetch(`${serverUrl}/process_logs`, {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({}),
              });
              const data = await res.json();
              if (!res.ok) {
                alert("Processing failed: " + (data.error || res.status));
                return;
              }
              alert(`Processed ${data.total_processed} sessions successfully.`);
            } catch (err) {
              alert("Request failed: " + err.message);
            }
          }}
        >
          Process Logs
        </button>
      </div>

      {/* FILTERS */}
      <div className="filters">

        <div className="filter">
          <label>Select Date:</label>
          <input
            type="date"
            value={selectedDate}
            max={getTodayInputValue()}
            onChange={(e) => setSelectedDate(e.target.value)}
          />
        </div>

        <div className="filter">
          <label>Select Cart:</label>
          <select value={selectedCart} onChange={(e) => setSelectedCart(e.target.value)}>
            <option value="">-- Optional --</option>
            {availableCarts.map((cart) => (
              <option key={cart} value={cart}>{cart}</option>
            ))}
          </select>
        </div>

        <div className="filter">
          <label>Select Session:</label>
          <select
            value={selectedSession}
            onChange={(e) => setSelectedSession(e.target.value)}
            disabled={!availableSessions.length}
          >
            <option value="">-- Optional --</option>
            {availableSessions.map((s) => (
              <option key={s} value={s}>{s}</option>
            ))}
          </select>
        </div>

        <div className="filter confirm-btn-container">
          <button onClick={confirmFilters} className="btn-primary">Confirm</button>
        </div>
      </div>

      {/* STATUS */}
      {loading && <p className="status-loading">Loading images...</p>}
      {error && <p className="status-error">{error}</p>}

      {/* IMAGE PAIRS DISPLAY */}
      {!loading && !error && imagePairs.length === 0 && (
        <p className="no-images-message">
          No images to be displayed for the selected filters.
        </p>
      )}

      {/* IMAGE PAIRS DISPLAY */}
      {imagePairs.length > 0 && (
        <div className="image-pairs">
          <p className="image-instructions">
            Click on images to enlarge them. If you would like to generate a combined heat map of multiple sessions, go to the Data Tab.
          </p>

          {imagePairs.map((pair, idx) => (
            <div key={idx} className="image-pair-card">
              
              {/* Compare checkbox */}
              <div className="compare-checkbox">
                <input
                  type="checkbox"
                  checked={compareSelections.includes(idx)}
                  onChange={(e) => {
                    if (e.target.checked) {
                      setCompareSelections([...compareSelections, idx]);
                    } else {
                      setCompareSelections(compareSelections.filter(i => i !== idx));
                    }
                  }}
                />
                <label>Compare</label>
              </div>

              <h3>Date: {pair.date}</h3>
              <h3>{pair.cart}</h3>
              <h3>Session {pair.session}</h3>

              <div className="image-pair-grid">
                {/* Heatmap */}
                <div className="image-container">
                  <p>Heat Map</p>
                  {pair.heatmap ? (
                    <img src={pair.heatmap} alt="Heatmap" onClick={() => setModalImage(pair.heatmap)} />
                  ) : (
                    <p className="no-image">No heatmap found</p>
                  )}
                </div>

                {/* Path */}
                <div className="image-container">
                  <p>Path</p>
                  {pair.path ? (
                    <img src={pair.path} alt="Path" onClick={() => setModalImage(pair.path)} />
                  ) : (
                    <p className="no-image">No path found</p>
                  )}
                </div>
              </div>
            </div>
          ))}
        </div>
      )}

      {modalImage && (
        <div className="image-modal" onClick={() => setModalImage(null)}>
          <img src={modalImage} alt="Enlarged" />
        </div>
      )}

      {compareSelections.length > 0 && (
        <div className="compare-panel">
          <h3>Compare Selected Sessions</h3>
          <div className="compare-grid">
            {compareSelections.map((idx) => {
              const pair = imagePairs[idx];
              return (
                <div key={idx} className="compare-card">
                  {/* Show Date, Cart, and Session */}
                  <div className="compare-card-header">
                    <p><strong>Date:</strong> {appliedDate}</p>
                    <p><strong>Cart:</strong> {pair.cart || "N/A"}</p>
                    <p><strong>Session:</strong> {pair.session}</p>
                  </div>
                  <div className="compare-images">
                    {pair.heatmap && (
                      <img
                        src={pair.heatmap}
                        alt="Heatmap"
                        onClick={() => setModalImage(pair.heatmap)}
                      />
                    )}
                    {pair.path && (
                      <img
                        src={pair.path}
                        alt="Path"
                        onClick={() => setModalImage(pair.path)}
                      />
                    )}
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      )}
    </div>
  );
}

export default HomeTab;
