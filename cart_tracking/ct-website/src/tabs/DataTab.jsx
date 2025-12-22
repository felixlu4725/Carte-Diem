import { useState, useEffect } from "react";
import "./DataTab.css";

const formatDateForAPI = (dateStr) => {
  const [year, month, day] = dateStr.split("-");
  return `${month}_${day}_${year.slice(-2)}`;
};

const getTodayInputValue = () => {
  const today = new Date();
  return today.toISOString().split("T")[0];
};

function DataTab({ storeData }) {
  const serverUrl = `http://35.3.245.73:${storeData?.server_port}`;

  const [selectedDates, setSelectedDates] = useState([]);
  const [availableCarts, setAvailableCarts] = useState([]);
  const [sessionData, setSessionData] = useState({});
  const [selectedSessions, setSelectedSessions] = useState([]);
  const [combinedImages, setCombinedImages] = useState(null);
  const [loadingCombine, setLoadingCombine] = useState(false);
  const [modalImage, setModalImage] = useState(null);

  const [error, setError] = useState(null);

  const handleDateChange = (e) => {
    const date = e.target.value;
    if (selectedDates.includes(date)) {
      setSelectedDates(selectedDates.filter(d => d !== date));
    } else {
      setSelectedDates([...selectedDates, date]);
    }
  };

  useEffect(() => {
    if (!selectedDates.length) {
      setAvailableCarts([]);
      setSessionData({});
      return;
    }

    const fetchData = async () => {
      try {
        let newSessionData = {};
        let allCarts = new Set();

        for (const date of selectedDates) {
          const dateKey = formatDateForAPI(date);

          const res = await fetch(`${serverUrl}/images/list?date=${dateKey}`);
          const data = await res.json();

          const carts = Object.keys(data.images?.[dateKey] || {});
          carts.forEach(c => allCarts.add(c));

          newSessionData[date] = {};
          carts.forEach(cart => {
            const cartData = data.images?.[dateKey]?.[cart] || {};
            const heatmaps = cartData.heatmaps || [];
            const paths = cartData.paths || [];

            const sessions = Array.from(
              new Set([
                ...heatmaps.map(f => f.replace("cart_heat_map_", "").replace(".png", "")),
                ...paths.map(f => f.replace("raw_path_points_", "").replace(".png", "")),
              ])
            );
            newSessionData[date][cart] = sessions;
          });
        }

        setAvailableCarts(Array.from(allCarts));
        setSessionData(newSessionData);
      } catch (err) {
        setError("Failed to fetch sessions");
        console.error(err);
      }
    };

    fetchData();
  }, [selectedDates, serverUrl]);

  const toggleSession = (date, cart, session) => {
    const exists = selectedSessions.some(
      s => s.date === date && s.cart === cart && s.session === session
    );

    if (exists) {
      setSelectedSessions(
        selectedSessions.filter(s => !(s.date === date && s.cart === cart && s.session === session))
      );
    } else {
      if (selectedSessions.length >= 10) return;

      const sessionKey = { date, cart, session };
      setSelectedSessions([...selectedSessions, sessionKey]);
    }
  };

  const handleCombine = async () => {
    if (selectedSessions.length === 0) return;

    setLoadingCombine(true);
    setCombinedImages(null);

    try {
      const sessionsParam = selectedSessions
      .map(({ date, cart, session }) => {
        const [year, month, day] = date.split('-');
        const formattedDate = `${month}_${day}_${year.slice(-2)}`;
        const cartFormatted = cart.startsWith("CART_") ? cart : `CART_${cart}`;
        const sessionFormatted = session.startsWith("session_") ? session : `session_${session}`;
        return `${formattedDate}/${cartFormatted}/${sessionFormatted}`;
      })
      .join(',');

      const res = await fetch(`${serverUrl}/combined_data?sessions=${encodeURIComponent(sessionsParam)}`);
      const data = await res.json();

      if (!res.ok || data.error) {
        alert(`Failed to combine: ${data.error || "Unknown error"}`);
        setLoadingCombine(false);
        return;
      }

      setCombinedImages({
        heatmap: `${serverUrl}${data.heatmap_url}`,
        pathmap: `${serverUrl}${data.pathmap_url}`,
      });
    } catch (err) {
      alert(`Request failed: ${err.message}`);
    } finally {
      setLoadingCombine(false);
    }
  };

  return (
    <div className="data-tab-container">
      <h2>Data Analytics</h2>

      {/* Content */}
      <div className="data-tab-content">
        {/* DATE SELECTION */}
        <div className="date-selection">
          <label>Select Dates (multi-select):</label>
          <input
            type="date"
            value={selectedDates[selectedDates.length - 1] || ""}
            max={getTodayInputValue()}
            onChange={handleDateChange}
          />
          <div className="selected-dates">
            {selectedDates.map(d => (
              <span key={d} className="date-chip">
                {d} 
                <button 
                  className="remove-date" 
                  onClick={() => setSelectedDates(selectedDates.filter(date => date !== d))}
                >
                  ×
                </button>
              </span>
            ))}
          </div>
        </div>

        {/* ERROR */}
        {error && <p className="error">{error}</p>}

        {/* SESSION TABLE */}
        {selectedDates.length > 0 && availableCarts.length > 0 && (
          <div className="session-table-container">
            <table className="session-table">
              <thead>
                <tr>
                  <th>Date</th>
                  <th>Cart</th>
                  <th>Sessions</th>
                </tr>
              </thead>
              <tbody>
                {[...selectedDates].sort((a, b) => new Date(a) - new Date(b)).map(date =>
                  availableCarts.map(cart =>
                    sessionData[date]?.[cart] ? (
                      <tr key={`${date}_${cart}`}>
                        <td>{date}</td>
                        <td>{cart}</td>
                        <td>
                          {[...sessionData[date][cart]]
                            .sort((a, b) => {
                              const numA = Number(a);
                              const numB = Number(b);
                              if (!isNaN(numA) && !isNaN(numB)) return numA - numB;
                              return a.localeCompare(b);
                            })
                            .map(session => {
                              const key = `${date}_${cart}_${session}`;
                              return (
                                <label key={key} className="session-checkbox">
                                  <input
                                    type="checkbox"
                                    checked={selectedSessions.some(s => s.date === date && s.cart === cart && s.session === session)}
                                    onChange={() => toggleSession(date, cart, session)}
                                    disabled={!selectedSessions.some(s => s.date === date && s.cart === cart && s.session === session) && selectedSessions.length >= 10}
                                  />
                                  {session}
                                </label>
                              );
                            })}
                        </td>
                      </tr>
                    ) : null
                  )
                )}
              </tbody>
            </table>
          </div>
        )}

        {/* COMBINE BUTTON */}
        <div className="combine-container">
          <button
            className="combine-button"
            disabled={selectedSessions.length === 0}
            onClick={handleCombine}
          >
            Combine
          </button>
          <p>{selectedSessions.length}/10 sessions selected</p>
        </div>

        {loadingCombine && <p>Generating combined images...</p>}

        {combinedImages && (
          <div className="combined-images">
            <h3>Combined Images</h3>
            <h2>After you leave this page, your generate image will go away. Please download the image if you want to save it.</h2>
            <div className="combined-images-row">
              <div className="combined-image">
                <p>Heatmap:</p>
                <img
                  src={combinedImages.heatmap}
                  alt="Combined Heatmap"
                  className="combined-img"
                  onClick={() => setModalImage(combinedImages.heatmap)}
                />
                <a href={combinedImages.heatmap} download="heatmap.png">Download Heatmap</a>
              </div>
              <div className="combined-image">
                <p>Path Map:</p>
                <img
                  src={combinedImages.pathmap}
                  alt="Combined Path Map"
                  className="combined-img"
                  onClick={() => setModalImage(combinedImages.pathmap)}
                />
                <a href={combinedImages.pathmap} download="pathmap.png">Download Pathmap</a>
              </div>
            </div>
          </div>
        )}

        {/* Modal for enlarged image */}
        {modalImage && (
          <div className="image-modal" onClick={() => setModalImage(null)}>
            <img src={modalImage} alt="Enlarged" />
          </div>
        )}
      </div>
    </div>
  );
}

export default DataTab;
