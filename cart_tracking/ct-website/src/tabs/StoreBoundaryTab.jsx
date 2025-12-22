import { useEffect, useRef, useState } from "react";
import L from "leaflet";
import "leaflet/dist/leaflet.css";
import "./StoreBoundaryTab.css";

export default function StoreBoundaryTab({ storeData }) {
  const mapRef = useRef(null);
  const mapContainerRef = useRef(null);
  const markersRef = useRef([]);
  const polylineRef = useRef(null);
  const polygonRef = useRef(null);
  const [coords, setCoords] = useState([]);

  // red dot icon
  const redDot = L.divIcon({
    className: "custom-red-dot",
    html: '<div style="width:12px;height:12px;background:red;border-radius:50%;border:2px solid white;box-shadow:0 0 4px rgba(0,0,0,0.5);"></div>',
    iconSize: [12, 12],
    iconAnchor: [6, 6],
  });

  const handleSendBoundaries = async () => {
    if (coords.length === 0) {
      alert("No boundary points to send!");
      return;
    }

    try {
      const res = await fetch(`http://35.3.245.73:${storeData.server_port}/boundaries`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ boundaries: coords.map(p => [p.lat, p.lon]) }),
      });

      // console.log(JSON.stringify({ boundaries: coords.map(p => [p.lat, p.lon]) }));

      if (!res.ok) {
        const errData = await res.json();
        throw new Error(errData.error || `Server responded with ${res.status}`);
      }

      const data = await res.json();
    } catch (err) {
      console.error("Error sending boundaries:", err);
      alert("Failed to save boundaries. Check console for details.");
    }
  };

  useEffect(() => {
    if (mapRef.current || !mapContainerRef.current) return;

    const map = L.map(mapContainerRef.current).setView([42.276928, -83.738233], 15);
    mapRef.current = map;

    L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
      attribution: "&copy; OpenStreetMap contributors",
      maxZoom: 19,
    }).addTo(map);

    const markers = markersRef.current;
    const polyline = L.polyline([], { color: "#0078ff", weight: 3 }).addTo(map);
    polylineRef.current = polyline;
    
    const polygon = L.polygon([], {
      color: "#0078ff",
      fillColor: "#66aaff",
      fillOpacity: 0.15,
      weight: 2,
    });
    polygonRef.current = polygon;

    const rebuild = () => {
      const latlngs = markers.map((m) => m.getLatLng());
      polyline.setLatLngs(latlngs);

      if (latlngs.length >= 3) {
        polygon.setLatLngs([latlngs]);
        if (!map.hasLayer(polygon)) polygon.addTo(map);
      } else {
        if (map.hasLayer(polygon)) map.removeLayer(polygon);
      }

      setCoords(latlngs.map((p) => ({ lat: p.lat, lon: p.lng })));
    };

    const addPoint = (latlng) => {
      const m = L.marker(latlng, { icon: redDot, draggable: true });
      m.on("drag", rebuild);
      m.on("dragend", rebuild);
      m.addTo(map);
      markers.push(m);
      rebuild();
    };

    map.on("click", (e) => addPoint(e.latlng));

    return () => {
      if (mapRef.current) {
        mapRef.current.remove();
        mapRef.current = null;
      }
    };
  }, []);

  const handleUndo = () => {
    const markers = markersRef.current;
    const m = markers.pop();
    if (m && mapRef.current) {
      mapRef.current.removeLayer(m);
      const latlngs = markers.map((marker) => marker.getLatLng());
      
      if (polylineRef.current) {
        polylineRef.current.setLatLngs(latlngs);
      }
      
      if (polygonRef.current && mapRef.current) {
        if (latlngs.length >= 3) {
          polygonRef.current.setLatLngs([latlngs]);
          if (!mapRef.current.hasLayer(polygonRef.current)) {
            polygonRef.current.addTo(mapRef.current);
          }
        } else {
          if (mapRef.current.hasLayer(polygonRef.current)) {
            mapRef.current.removeLayer(polygonRef.current);
          }
        }
      }
      
      setCoords(latlngs.map((p) => ({ lat: p.lat, lon: p.lng })));
    }
  };

  const handleClear = () => {
    const markers = markersRef.current;
    if (mapRef.current) {
      while (markers.length) {
        const m = markers.pop();
        if (m) mapRef.current.removeLayer(m);
      }
      
      if (polylineRef.current) {
        polylineRef.current.setLatLngs([]);
      }
      
      if (polygonRef.current && mapRef.current.hasLayer(polygonRef.current)) {
        mapRef.current.removeLayer(polygonRef.current);
      }
      
      setCoords([]);
    }
  };

  const handleCopy = async () => {
    try {
      await navigator.clipboard.writeText(cSnippet);
      alert("Copied to clipboard!");
    } catch (err) {
      console.error("Copy failed:", err);
      alert("Copy failed. Please copy manually.");
    }
  };

  const fmt = (n) => n.toFixed(6);

  const cSnippet = coords.length > 0 
    ? `static const gps_coord_t boundary[] = {
${coords
  .map((p, i) => `  {${fmt(p.lat)}, ${fmt(p.lon)}}${i < coords.length - 1 ? "," : ""}`)
  .join("\n")}
};`
    : `// Click on the map to add boundary points`;

  return (
    <div className="gps-boundary-container-column">
      <div className="panel">
        <h2>{storeData?.store_name || "Store Boundary"} - Points: {coords.length}</h2>

        <div className="instructions">
          <p>📍 Click on the map to add boundary points</p>
          <p>🖱️ Drag markers to adjust positions</p>
        </div>
        <div className="panel-buttons">
          <button onClick={handleUndo} disabled={coords.length === 0}>
            Undo Last Point
          </button>
          <button onClick={handleClear} disabled={coords.length === 0}>
            Clear All
          </button>
          <button onClick={handleCopy} disabled={coords.length === 0}>
            Copy C Snippet
          </button>
        </div>
        <div className="panel-map-container">
          <div ref={mapContainerRef} className="map-container" />
          <textarea readOnly value={cSnippet} className="c-snippet" />
        </div>
        <div style={{ display: "flex", justifyContent: "flex-end", marginTop: "8px" }}>
          <button
              onClick={handleSendBoundaries}
              disabled={coords.length === 0 || !storeData?.server_port}
            >
              Send Boundaries
            </button>
        </div>
        
      </div>
    </div>
  );
}