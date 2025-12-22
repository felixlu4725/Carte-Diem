import { useState, useEffect } from "react";
import { FaUpload, FaSpinner, FaCheckCircle } from "react-icons/fa";
import "./MapTab.css";

function MapTab({ storeData }) {
  const [selectedFileType, setSelectedFileType] = useState(null);
  const [selectedFile, setSelectedFile] = useState(null);
  const [previewUrl, setPreviewUrl] = useState(null);
  const [uploading, setUploading] = useState(false);
  const [uploadError, setUploadError] = useState(null);
  const [uploadSuccess, setUploadSuccess] = useState(false);

  const [storeWidth, setStoreWidth] = useState("");
  const [widthEntered, setWidthEntered] = useState(false);
  const [editingWidth, setEditingWidth] = useState(false);
  const [tempWidth, setTempWidth] = useState("");

  const [tagSpacing, setTagSpacing] = useState("");
  const [spacingEntered, setSpacingEntered] = useState(false);
  const [editingSpacing, setEditingSpacing] = useState(false);
  const [tempSpacing, setTempSpacing] = useState("");

  const [widthChangeNotice, setWidthChangeNotice] = useState(false);
  const [spacingChangeNotice, setSpacingChangeNotice] = useState(false);

  useEffect(() => {
    async function fetchSchematic() {
      try {
        const res = await fetch(`http://35.3.245.73:${storeData.server_port}/store/schematic/processed`);
        if (res.ok) {
          const blob = await res.blob();
          const url = URL.createObjectURL(blob);
          setPreviewUrl(url);
        }
      } catch (err) {
        console.error("Failed to fetch schematic:", err);
      }
    }

    fetchSchematic();
  }, []);

  const handleFileSelect = (e) => {
    const file = e.target.files[0];
    if (!file) return;

    const fileExt = file.name.split('.').pop().toLowerCase();
    if (!['png', 'pdf', 'bmp'].includes(fileExt)) {
      setUploadError("Please select a PNG, PDF, or BMP file");
      return;
    }

    setSelectedFile(file);
    setSelectedFileType(fileExt);
    setUploadError(null);
    setUploadSuccess(false);

    setPreviewUrl(URL.createObjectURL(file));
  };

  const handleUpload = async () => {
    if (!selectedFile) {
      setUploadError("Please select a file first");
      return;
    }

    if (!widthEntered || !spacingEntered) {
      setUploadError("Please enter both store width and tag spacing before processing");
      return;
    }

    setUploading(true);
    setUploadError(null);
    setUploadSuccess(false);

    const formData = new FormData();
    formData.append('store_schematic', selectedFile);
    formData.append('store_width', storeWidth);
    formData.append('tag_spacing', tagSpacing);

    try {
      const res = await fetch(`http://35.3.245.73:${storeData.server_port}/input_schematic`, {
        method: 'POST',
        body: formData,
      });

      const data = await res.json();

      if (data.success) {
        setUploadSuccess(true);
        const imgRes = await fetch(`http://35.3.245.73:${storeData.server_port}/store/schematic/processed`);
        if (!imgRes.ok) throw new Error('Failed to fetch processed schematic');
        const blob = await imgRes.blob();
        setPreviewUrl(URL.createObjectURL(blob));
      } else {
        setUploadError(data.error || "Failed to process schematic");
      }
    } catch (err) {
      console.error("Error uploading schematic:", err);
      setUploadError("Failed to upload and process schematic");
    } finally {
      setUploading(false);
    }
  };

  const handleClear = () => {
    setSelectedFile(null);
    setPreviewUrl(null);
    setSelectedFileType(null);
    setUploadError(null);
    setUploadSuccess(false);
  };

  return (
    <div className="map-container">
      <h2>Store Schematic Upload</h2>
      <p className="map-description">
        Must enter both store width and tag spacing, then upload your store schematic to process RFID tag positions.
      </p>
      <p>If unsure, store width = 500 and tag spacing = 10.</p>

      <div className="map-upload-section">
        <div className="upload-controls">

          {/* Store Width Input */}
          {!widthEntered ? (
            <div className="width-prompt">
              <label>Enter Store Width:</label>
              <input
                type="number"
                value={storeWidth}
                onChange={(e) => setStoreWidth(e.target.value)}
                placeholder="Width in feet"
              />
              <button
                onClick={() => {
                  if (storeWidth && parseFloat(storeWidth) > 0) {
                    setWidthEntered(true);
                  } else alert("Please enter a valid store width");
                }}
              >
                Confirm Width
              </button>
            </div>
          ) : (
            <div className="width-display">
              <strong>Store Width:</strong> {storeWidth} feet
              <button onClick={() => { setEditingWidth(true); setTempWidth(storeWidth); }}>Change Width</button>
            </div>
          )}

          {/* Tag Spacing Input */}
          {!spacingEntered ? (
            <div className="width-prompt">
              <label>Enter Tag Spacing:</label>
              <input
                type="number"
                value={tagSpacing}
                onChange={(e) => setTagSpacing(e.target.value)}
                placeholder="Spacing in units"
              />
              <button
                onClick={() => {
                  if (tagSpacing && parseFloat(tagSpacing) > 0) {
                    setSpacingEntered(true);
                  } else alert("Please enter a valid tag spacing");
                }}
              >
                Confirm Spacing
              </button>
            </div>
          ) : (
            <div className="width-display">
              <strong>Tag Spacing:</strong> {tagSpacing}
              <button onClick={() => { setEditingSpacing(true); setTempSpacing(tagSpacing); }}>Change Spacing</button>
            </div>
          )}

          {/* File Input */}
          <input
            type="file"
            accept=".png, .pdf, .bmp"
            onChange={handleFileSelect}
            id="map-file-input"
            className="file-input"
            disabled={!widthEntered || !spacingEntered}
          />
          <label htmlFor="map-file-input" className="file-input-label">
            <FaUpload /> Choose Schematic (PNG/PDF/BMP)
          </label>

          {/* File Info */}
          {selectedFile && (
            <div className="file-info">
              <span className="file-name">{selectedFile.name}</span>
              <span className="file-size">
                ({(selectedFile.size / 1024).toFixed(2)} KB)
              </span>
            </div>
          )}

          {/* Upload Actions */}
          {selectedFile && (
            <div className="upload-actions">
              <button
                onClick={handleUpload}
                disabled={uploading || !widthEntered || !spacingEntered}
                className="upload-btn"
              >
                {uploading ? (
                  <>
                    <FaSpinner className="spinner" /> Processing...
                  </>
                ) : (
                  <>
                    <FaUpload /> Process Schematic
                  </>
                )}
              </button>
              <button onClick={handleClear} className="clear-btn" disabled={uploading}>
                Clear
              </button>
            </div>
          )}
        </div>

        {uploadError && <div className="error-message">{uploadError}</div>}
        {widthChangeNotice && <div className="info-message">Store width updated. Please upload and process your schematic again.</div>}
        {spacingChangeNotice && <div className="info-message">Tag spacing updated. Please upload and process your schematic again.</div>}
        {uploadSuccess && <div className="success-message"><FaCheckCircle /> Schematic processed successfully! RFID positions have been generated.</div>}

        {previewUrl && (
          <div className="map-display">
            <h3>{selectedFile && !uploadSuccess ? "Raw Schematic" : "Processed RFID Schematic"}</h3>
            {selectedFileType === 'pdf' ? (
              <iframe
                src={`${previewUrl}#toolbar=0&navpanes=0&scrollbar=0`}
                title="PDF Schematic"
                width="100%"
                height="600px"
              />
            ) : (
              <img src={previewUrl} alt="Schematic preview" style={{ maxWidth: "100%", height: "auto" }} />
            )}
          </div>
        )}
      </div>
    </div>
  );
}

export default MapTab;
