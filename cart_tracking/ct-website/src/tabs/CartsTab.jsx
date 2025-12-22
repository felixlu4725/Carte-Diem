import { useState, useEffect } from "react";
import "./CartsTab.css";

function CartsTab({ storeData }) {
  const serverUrl = `http://35.3.245.73:${storeData?.server_port}`;
  const [carts, setCarts] = useState([]);
  const [error, setError] = useState(null);

  useEffect(() => {
    const fetchCarts = async () => {
      try {
        const res = await fetch(`${serverUrl}/carts_registered`);
        const data = await res.json();
        if (!res.ok) {
          setError(data.error || "Failed to fetch cart info");
          return;
        }
        setCarts(data.carts || []);
      } catch (err) {
        setError(err.message);
      }
    };

    fetchCarts();
  }, [serverUrl]);

  return (
    <div className="carts-tab-container">
      <h2>Cart Management</h2>

      {error && <p className="error">{error}</p>}

      {carts.length > 0 ? (
        <table className="carts-table">
          <thead>
            <tr>
              <th>Hardware ID</th>
              <th>Cart ID</th>
            </tr>
          </thead>
          <tbody>
            {carts.map(({ hardware_id, cart_id }) => (
              <tr key={cart_id}>
                <td>{hardware_id}</td>
                <td>{cart_id}</td>
              </tr>
            ))}
          </tbody>
        </table>
      ) : (
        <p className="no-carts">No carts are currently registered.</p>
      )}
    </div>
  );
}

export default CartsTab;
