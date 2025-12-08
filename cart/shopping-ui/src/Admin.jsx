import React, { useState, useEffect } from "react";
import { useNavigate } from "react-router-dom";
import "./Admin.css";

const Admin = ({ items, setItems, showWorkerHelp, setShowWorkerHelp, verificationStatus, setVerificationStatus, verificationInfo, setVerificationInfo }) => {
  const navigate = useNavigate();
  const [statusMessage, setStatusMessage] = useState("");
  const [statusType, setStatusType] = useState("");
  const [confirmReset, setConfirmReset] = useState(false);
  const [activeSection, setActiveSection] = useState("cart");
  const [activeTab, setActiveTab] = useState("addProduct");
  const [products, setProducts] = useState([]);
  const [editingProduct, setEditingProduct] = useState(null);
  const [editFormData, setEditFormData] = useState({});
  const [deleteConfirmOpen, setDeleteConfirmOpen] = useState(false);
  const [upcToDelete, setUpcToDelete] = useState(null);
  const [wizardStep, setWizardStep] = useState(0);
  const [queryResult, setQueryResult] = useState("");
  const [isCollapsed, setIsCollapsed] = useState(false);
  const [masterIp, setMasterIp] = useState("");
  const [masterPort, setMasterPort] = useState("");
  const [companyIp, setCompanyIp] = useState("");
  const [companyPort, setCompanyPort] = useState("");
  const [loading, setLoading] = useState(true);

  const sidebarWidth = isCollapsed ? "60px" : "250px";

  const [productData, setProductData] = useState({
    upc: "",
    brand: "",
    description: "",
    qty: "",
    price: "",
    weight: "",
    produce: "",
    aisle: "",
    section: "",
    side: "",
    aisleDescription: "",
    categories: [],
    newCategory: "",
    images: [{ size: "", url: "" }],
    rfidTags: [],
    newRFID: "",
  });

  const wizardSteps = [
    "Product Info",
    "Aisle Location",
    "RFID Tags",
    "Categories",
    "Images",
  ];

  const resetProductData = () => {
    setProductData({
      upc: "",
      brand: "",
      description: "",
      qty: "",
      price: "",
      weight: "",
      produce: "",
      aisle: "",
      section: "",
      side: "",
      aisleDescription: "",
      categories: [],
      newCategory: "",
      images: [{ size: "", url: "" }],
      rfidTags: [],
      newRFID: "",
    });
    setWizardStep(0);
  };

  const fetchFullProducts = async () => {
    try {
      const query = `
        SELECT 
            p.upc, p.brand, p.description, p.qty, p.price, p.weight, p.produce,
            GROUP_CONCAT(DISTINCT c.name) AS categories,
            GROUP_CONCAT(DISTINCT a.aisle || '|' || a.section || '|' || a.side || '|' || a.description) AS aisleLocations,
            GROUP_CONCAT(DISTINCT i.url || '|' || i.size) AS images,
            GROUP_CONCAT(DISTINCT r.rfid_id) AS rfidTags
        FROM Products p
        LEFT JOIN ProductCategories pc ON p.upc = pc.upc
        LEFT JOIN Categories c ON pc.category_id = c.id
        LEFT JOIN AisleLocations a ON p.upc = a.upc
        LEFT JOIN Images i ON p.upc = i.upc
        LEFT JOIN ProductRFIDs r ON p.upc = r.upc
        GROUP BY p.upc
      `;

      const result = await window.electronAPI.runPython(
        "cart_ops.py",
        ["execute_sql", query]
      );

      const products = (result.results || []).map((p) => ({
        ...p,
        categories: p.categories ? p.categories.split(",") : [],
        aisleLocations: p.aisleLocations
          ? p.aisleLocations.split(",").map((loc) => {
              const [aisle, section, side, description] = loc.split("|");
              return { aisle, section, side, description };
            })
          : [],
        images: p.images
          ? p.images.split(",").map((img) => {
              const [url, size] = img.split("|");
              return { url, size };
            })
          : [],
        rfidTags: p.rfidTags ? p.rfidTags.split(",") : [],
      }));

      setProducts(products);
    } catch (err) {
      console.error("Failed to load products:", err);
      setProducts([]);
    }
  };

  const handleAddCompleteProduct = async () => {
    try {
      const payload = {
        product: {
          upc: productData.upc,
          brand: productData.brand,
          description: productData.description,
          qty: parseInt(productData.qty),
          price: parseFloat(productData.price),
          weight: parseFloat(productData.weight),
          produce: parseInt(productData.produce),
        },
        aisleLocation: {
          upc: productData.upc,
          aisle: productData.aisle,
          section: productData.section,
          side: productData.side,
          description: productData.aisleDescription,
        },
        categories: productData.categories,
        images: productData.images.filter((img) => img.size && img.url),
        rfidTags: productData.rfidTags,
      };

      const data = await window.electronAPI.runPython(
        "cart_ops.py",
        ["add_complete_product", JSON.stringify(payload)]
      );

      if (data.status === "success") {
        setStatusMessage("Product added successfully!");
        setStatusType("success");
        resetProductData();
      } else {
        setStatusMessage(`Error: ${data.message}`);
        setStatusType("error");
      }

      setQueryResult(JSON.stringify(data, null, 2));
    } catch (err) {
      setQueryResult(`Error: ${err.message}`);
      setStatusMessage(`Error: ${err.message}`);
      setStatusType("error");
    }
  };

  const addImageField = () => {
    setProductData({
      ...productData,
      images: [...productData.images, { size: "", url: "" }],
    });
  };

  const updateImage = (index, field, value) => {
    const newImages = [...productData.images];
    newImages[index][field] = value;
    setProductData({ ...productData, images: newImages });
  };

  const removeImageField = (index) => {
    const newImages = productData.images.filter((_, i) => i !== index);
    setProductData({ ...productData, images: newImages });
  };

  const addCategory = () => {
    if (productData.newCategory.trim()) {
      setProductData({
        ...productData,
        categories: [...productData.categories, productData.newCategory.trim()],
        newCategory: "",
      });
    }
  };

  const removeCategory = (index) => {
    setProductData({
      ...productData,
      categories: productData.categories.filter((_, i) => i !== index),
    });
  };

  const addRFID = () => {
    if (productData.newRFID.trim()) {
      setProductData({
        ...productData,
        rfidTags: [...productData.rfidTags, productData.newRFID.trim()],
        newRFID: "",
      });
    }
  };

  const removeRFID = (index) => {
    setProductData({
      ...productData,
      rfidTags: productData.rfidTags.filter((_, i) => i !== index)
    });
  };

  const handleResetCart = async () => {
    try {
      const data = await window.electronAPI.runPython(
        "cart_ops.py",
        ["end_session"]
      );

      await window.electronAPI.sendStdinCommand("CT_CLEAR");

      if (data && data.status === "success") {
        setStatusMessage("Cart has been reset successfully!");
        setStatusType("success");
        setConfirmReset(false);
        setTimeout(() => navigate("/"), 1500);
      } else {
        const errMsg = data?.message || "Empty or invalid response from Python";
        throw new Error(errMsg);
      }
    } catch (err) {
      console.error("Error resetting cart:", err);
      setStatusMessage(`Error resetting cart: ${err.message}`);
      setStatusType("error");
    }
  };

  useEffect(() => {
    const fetchSetup = async () => {
      try {
        const result = await window.electronAPI.runPython("cart_ops.py", ["get_setup"]);
        if (result.status === "success") {
          setMasterIp(result.master_ip || "");
          setMasterPort(result.master_port || "");
          setCompanyIp(result.company_ip || "");
          setCompanyPort(result.company_port || "");
        } else {
          console.error("Failed to fetch setup:", result);
        }
      } catch (err) {
        console.error("Error fetching setup:", err);
      } finally {
        setLoading(false);
      }
    };
    fetchSetup();
  }, []);

  const handleSave = async () => {
    try {
      const result = await window.electronAPI.runPython("cart_ops.py", [
        "save_setup",
        masterIp,
        masterPort,
        companyIp,
        companyPort,
      ]);
      if (result.status === "success") {
        alert("Setup updated successfully!");
      } else {
        alert("Failed to update setup.");
        console.error(result);
      }
    } catch (err) {
      console.error("Error saving setup:", err);
      alert("Error saving setup. Check console.");
    }
  };

  if (loading) return <div>Loading setup...</div>;

  return (
    <div style={{ display: "flex", height: "100vh" }}>
      {/* Sidebar */}
      <div
        style={{
          width: sidebarWidth,
          height: "100vh",
          backgroundColor: "#2c3e50",
          color: "white",
          padding: "20px",
          display: "flex",
          flexDirection: "column",
          position: "fixed",
          top: 0,
          left: 0,
          transition: "width 0.3s ease",
          overflow: "hidden",
        }}
      >
        {/* Header with collapse button */}
        <div
          style={{
            display: "flex",
            alignItems: "center",
            justifyContent: isCollapsed ? "center" : "space-between",
            marginBottom: "30px",
          }}
        >
          {!isCollapsed && (
            <h2 style={{ fontSize: "20px", margin: 0 }}>Admin Panel</h2>
          )}
          <button
            onClick={() => setIsCollapsed(!isCollapsed)}
            style={{
              backgroundColor: "transparent",
              border: "none",
              color: "white",
              fontSize: "18px",
              cursor: "pointer",
              marginLeft: isCollapsed ? 0 : "10px",
            }}
          >
            {isCollapsed ? ">" : "<"}
          </button>
        </div>

        {/* Menu Buttons */}
        <button
          onClick={() => setActiveSection("cart")}
          style={{
            display: "flex",
            alignItems: "center",
            gap: isCollapsed ? "0" : "10px",
            justifyContent: isCollapsed ? "center" : "flex-start",
            padding: "12px 15px",
            marginBottom: "10px",
            backgroundColor: activeSection === "cart" ? "#3498db" : "transparent",
            color: "white",
            border: "none",
            borderRadius: "5px",
            cursor: "pointer",
            fontSize: "16px",
            transition: "background-color 0.3s, gap 0.3s",
          }}
        >
          🛒 {!isCollapsed && "Cart Functions"}
        </button>

        <button
          onClick={() => setActiveSection("database")}
          style={{
            display: "flex",
            alignItems: "center",
            gap: isCollapsed ? "0" : "10px",
            justifyContent: isCollapsed ? "center" : "flex-start",
            padding: "12px 15px",
            marginBottom: "10px",
            backgroundColor:
              activeSection === "database" ? "#3498db" : "transparent",
            color: "white",
            border: "none",
            borderRadius: "5px",
            cursor: "pointer",
            fontSize: "16px",
            transition: "background-color 0.3s, gap 0.3s",
          }}
        >
          🗄️ {!isCollapsed && "Database Management"}
        </button>

        <button
          onClick={() => setActiveSection("misc")}
          style={{
            display: "flex",
            alignItems: "center",
            gap: isCollapsed ? "0" : "10px",
            justifyContent: isCollapsed ? "center" : "flex-start",
            padding: "12px 15px",
            marginBottom: "10px",
            backgroundColor:
              activeSection === "misc" ? "#3498db" : "transparent",
            color: "white",
            border: "none",
            borderRadius: "5px",
            cursor: "pointer",
            fontSize: "16px",
            transition: "background-color 0.3s, gap 0.3s",
          }}
        >
          🛠️ {!isCollapsed && "Misc"}
        </button>

        <button
          onClick={() => setActiveSection("setup")}
          style={{
            display: "flex",
            alignItems: "center",
            gap: isCollapsed ? "0" : "10px",
            justifyContent: isCollapsed ? "center" : "flex-start",
            padding: "12px 15px",
            marginBottom: "10px",
            backgroundColor:
              activeSection === "setup" ? "#3498db" : "transparent",
            color: "white",
            border: "none",
            borderRadius: "5px",
            cursor: "pointer",
            fontSize: "16px",
            transition: "background-color 0.3s, gap 0.3s",
          }}
        >
          ⚙️ {!isCollapsed && "Setup"}
        </button>

        {/* Footer Button */}
        <div style={{ marginTop: "auto" }}>
          <button
            onClick={() => navigate("/cart")}
            style={{
              width: "100%",
              padding: "10px",
              backgroundColor: "#7f8c8d",
              border: "none",
              color: "white",
              borderRadius: "5px",
              cursor: "pointer",
              display: "flex",
              alignItems: "center",
              justifyContent: isCollapsed ? "center" : "flex-start",
              gap: isCollapsed ? "0" : "8px",
              transition: "all 0.3s",
            }}
          >
            ← {!isCollapsed && "Back to Cart"}
          </button>
        </div>
      </div>

      {/* Main Content Area */}
      <div
        style={{
          position: "absolute",
          top: 0,
          left: sidebarWidth,
          width: `calc(100% - ${sidebarWidth})`,
          height: "100vh",
          padding: "30px",
          overflowY: "auto",
          transition: "left 0.3s, width 0.3s",
        }}
      >
        {statusMessage && (
          <div
            style={{
              color: statusType === "success" ? "green" : "red",
              marginBottom: "20px",
              padding: "15px",
              backgroundColor: statusType === "success" ? "#d4edda" : "#f8d7da",
              borderRadius: "5px",
              border: `1px solid ${statusType === "success" ? "#c3e6cb" : "#f5c6cb"}`,
            }}
          >
            {statusMessage}
          </div>
        )}

        {/* Cart Functions Section */}
        {activeSection === "cart" && (
          <div>
            <h2 style={{ marginBottom: "20px" }}>Cart Functions</h2>

            <div
              style={{
                backgroundColor: "white",
                padding: "25px",
                borderRadius: "8px",
                boxShadow: "0 2px 4px rgba(0,0,0,0.1)",
              }}
            >
              <h3 style={{ marginBottom: "10px" }}>🛒 Reset Cart</h3>
              <p style={{ color: "#666", marginBottom: "20px", lineHeight: "1.5" }}>
                Clear all items from the current shopping cart. This action cannot be undone.
              </p>
              <button
                className="btn"
                onClick={() => setConfirmReset(true)}
                style={{
                  backgroundColor: "#dc3545",
                  color: "white",
                  padding: "12px 24px",
                  border: "none",
                  borderRadius: "5px",
                  cursor: "pointer",
                  fontSize: "16px",
                  marginBottom: "10px" // spacing between buttons
                }}
              >
                Reset Cart
              </button>

              <h3 style={{ marginBottom: "10px" }}>🛒 Resolve Restricted Items</h3>
              <p style={{ color: "#666", marginBottom: "20px", lineHeight: "1.5" }}>
                Marks all items that require verification as resolved. Must be done before checkout.
              </p>
              <button
                className="btn"
                onClick={async () => {
                  try {
                    const result = await window.electronAPI.runPython(
                      "cart_ops.py",
                      ["resolve_all_required_items"]
                    );
                    console.log("Resolve All result:", result);
                    setShowWorkerHelp(false);
                  } catch (err) {
                    console.error("Error resolving items:", err);
                  }
                }}
                style={{
                  backgroundColor: "#dcb535ff",
                  color: "white",
                  padding: "12px 24px",
                  border: "none",
                  borderRadius: "5px",
                  cursor: "pointer",
                  fontSize: "16px"
                }}
              >
                Resolve All Restricted Items
              </button>

              <h3 style={{ marginBottom: "10px", marginTop: "20px" }}>🛠️ Clear Worker Help</h3>
              <p style={{ color: "#666", marginBottom: "20px", lineHeight: "1.5" }}>
                Check the items in the cart. Dismiss the worker help notification manually.
              </p>
              <button
                className="btn"
                onClick={() => {
                  setShowWorkerHelp(false);
                  setVerificationStatus("success");
                }}
                style={{
                  backgroundColor: "#6c757d",
                  color: "white",
                  padding: "12px 24px",
                  border: "none",
                  borderRadius: "5px",
                  cursor: "pointer",
                  fontSize: "16px"
                }}
              >
                Clear Worker Help
              </button>
            </div>
          </div>
        )}

        {/* Database Management Section */}
        {activeSection === "database" && (
          <div>
            <h2 style={{ marginBottom: "20px" }}>Database Management</h2>

            <div className="admin-main-tabs">
              <button
                className={`admin-main-tab ${activeTab === "addProduct" ? "active" : ""}`}
                onClick={() => setActiveTab("addProduct")}
              >
                Add Product
              </button>
              <button
                className={`admin-main-tab ${activeTab === "viewProducts" ? "active" : ""}`}
                onClick={() => {
                  setActiveTab("viewProducts");
                  fetchFullProducts();
                }}
              >
                View Products
              </button>
              <button
                className={`admin-main-tab ${activeTab === "importCSV" ? "active" : ""}`}
                onClick={() => setActiveTab("importCSV")}
              >
                Import CSV
              </button>
            </div>

            {/* IMPORT CSV TAB */}
            {activeTab === "importCSV" && (
              <div style={{ padding: "20px" }}>
                <h3>Import Products from CSV</h3>
                <p style={{ color: "#666", marginBottom: "20px" }}>
                  Upload a CSV file with complete product data including aisle locations, categories, and images.
                </p>

                <input
                  type="file"
                  accept=".csv"
                  onChange={async (e) => {
                    const file = e.target.files[0];
                    if (!file) return;

                    try {
                      setStatusMessage("Uploading and processing CSV...");
                      setStatusType("success");

                      const text = await file.text();

                      const data = await window.electronAPI.runPython(
                        "cart_ops.py",
                        ["import_csv", JSON.stringify(text)]
                      );

                      if (data.status === "success") {
                        setStatusMessage(`Success! Imported ${data.imported || 0} products.`);
                        setStatusType("success");
                      } else {
                        setStatusMessage(`Error: ${data.message}`);
                        setStatusType("error");
                      }

                      setQueryResult(JSON.stringify(data, null, 2));
                    } catch (err) {
                      console.error("CSV import error:", err);
                      setStatusMessage(`Error: ${err.message}`);
                      setStatusType("error");
                    }

                    e.target.value = "";
                  }}
                  style={{
                    padding: "12px",
                    fontSize: "14px",
                    border: "2px dashed #ccc",
                    borderRadius: "8px",
                    width: "100%",
                    cursor: "pointer",
                    backgroundColor: "#f9f9f9",
                  }}
                />

                <div
                  style={{
                    marginTop: "30px",
                    padding: "20px",
                    backgroundColor: "#f8f9fa",
                    borderRadius: "8px",
                    border: "1px solid #dee2e6",
                  }}
                >
                  <h4 style={{ marginTop: 0 }}>📋 CSV Format Example:</h4>
                  <p style={{ fontSize: "13px", color: "#666", marginBottom: "15px" }}>
                    <strong>Required columns:</strong> UPC, Brand, Description, Qty, Price, Weight, Produce
                    <br />
                    <strong>Optional columns:</strong> Aisle, Section, Side, AisleDescription, Categories
                    (comma-separated), ImageSize, ImageURL
                  </p>
                  <pre
                    style={{
                      fontSize: "12px",
                      overflow: "auto",
                      backgroundColor: "#fff",
                      padding: "15px",
                      borderRadius: "5px",
                      border: "1px solid #dee2e6",
                    }}
                  >
                    {`UPC,Brand,Description,Qty,Price,Weight,Produce,Aisle,Section,Side,AisleDescription,Categories,ImageSize,ImageURL
123456789,Acme,Organic Apples,50,2.99,16,1,A1,3,Left,Fresh Produce Section,"Fruit,Organic,Fresh",medium,https://example.com/apple.jpg
987654321,Brand X,Cereal Box,30,4.50,12,0,B5,2,Right,Breakfast Aisle,"Cereal,Breakfast,Grain",large,https://example.com/cereal.jpg`}
                  </pre>
                </div>
              </div>
            )}

            {/* ADD PRODUCT TAB */}
            {activeTab === "addProduct" && (
              <div>
                <div className="wizard-progress">
                  {wizardSteps.map((step, index) => (
                    <div
                      key={index}
                      className={`wizard-step ${
                        index === wizardStep ? "active" : index < wizardStep ? "completed" : ""
                      }`}
                    >
                      {step}
                    </div>
                  ))}
                </div>

                {/* Step 0: Product Info */}
                {wizardStep === 0 && (
                  <div>
                    <h3>Product Information</h3>
                    <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "10px", marginBottom: "20px" }}>
                      <input
                        type="text"
                        placeholder="UPC *"
                        value={productData.upc}
                        onChange={(e) => setProductData({ ...productData, upc: e.target.value })}
                        style={{ padding: "8px", fontSize: "14px" }}
                      />
                      <input
                        type="text"
                        placeholder="Brand"
                        value={productData.brand}
                        onChange={(e) => setProductData({ ...productData, brand: e.target.value })}
                        style={{ padding: "8px", fontSize: "14px" }}
                      />
                      <input
                        type="text"
                        placeholder="Description *"
                        value={productData.description}
                        onChange={(e) => setProductData({ ...productData, description: e.target.value })}
                        style={{ padding: "8px", fontSize: "14px", gridColumn: "1 / -1" }}
                      />
                      <input
                        type="number"
                        placeholder="Quantity *"
                        value={productData.qty}
                        onChange={(e) => setProductData({ ...productData, qty: e.target.value })}
                        style={{ padding: "8px", fontSize: "14px" }}
                      />
                      <input
                        type="number"
                        step="0.01"
                        placeholder="Price *"
                        value={productData.price}
                        onChange={(e) => setProductData({ ...productData, price: e.target.value })}
                        style={{ padding: "8px", fontSize: "14px" }}
                      />
                      <input
                        type="number"
                        step="0.01"
                        placeholder="Weight (oz) *"
                        value={productData.weight}
                        onChange={(e) => setProductData({ ...productData, weight: e.target.value })}
                        style={{ padding: "8px", fontSize: "14px" }}
                      />
                      <input
                        type="number"
                        placeholder="Produce (0 or 1) *"
                        value={productData.produce}
                        onChange={(e) => setProductData({ ...productData, produce: e.target.value })}
                        style={{ padding: "8px", fontSize: "14px" }}
                      />
                    </div>
                    <div style={{ display: "flex", gap: "10px", justifyContent: "flex-end" }}>
                      <button className="btn" onClick={() => setWizardStep(1)} style={{ backgroundColor: "#0d6efd" }}>
                        Next →
                      </button>
                    </div>
                  </div>
                )}

                {/* Step 1: Aisle Location */}
                {wizardStep === 1 && (
                  <div>
                    <h3>Aisle Location (Optional)</h3>
                    <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "10px", marginBottom: "20px" }}>
                      <input
                        type="text"
                        placeholder="Aisle"
                        value={productData.aisle}
                        onChange={(e) => setProductData({ ...productData, aisle: e.target.value })}
                        style={{ padding: "8px", fontSize: "14px" }}
                      />
                      <input
                        type="text"
                        placeholder="Section"
                        value={productData.section}
                        onChange={(e) => setProductData({ ...productData, section: e.target.value })}
                        style={{ padding: "8px", fontSize: "14px" }}
                      />
                      <input
                        type="text"
                        placeholder="Side"
                        value={productData.side}
                        onChange={(e) => setProductData({ ...productData, side: e.target.value })}
                        style={{ padding: "8px", fontSize: "14px" }}
                      />
                      <input
                        type="text"
                        placeholder="Location Description"
                        value={productData.aisleDescription}
                        onChange={(e) => setProductData({ ...productData, aisleDescription: e.target.value })}
                        style={{ padding: "8px", fontSize: "14px" }}
                      />
                    </div>
                    <div style={{ display: "flex", gap: "10px", justifyContent: "space-between" }}>
                      <button className="btn cancel-btn" onClick={() => setWizardStep(0)}>← Back</button>
                      <button className="btn" onClick={() => setWizardStep(2)} style={{ backgroundColor: "#0d6efd" }}>Next →</button>
                    </div>
                  </div>
                )}

                {/* Step 2: RFID Tags */}
                {wizardStep === 2 && (
                  <div>
                    <h3>RFID Tags (Optional)</h3>
                    <div style={{ marginBottom: "20px" }}>
                      <div style={{ display: "flex", gap: "10px", marginBottom: "15px" }}>
                        <input
                          type="text"
                          placeholder="Enter RFID Tag"
                          value={productData.newRFID}
                          onChange={(e) => setProductData({ ...productData, newRFID: e.target.value })}
                          onKeyPress={(e) => e.key === "Enter" && addRFID()}
                          style={{ padding: "8px", fontSize: "14px", flex: 1 }}
                        />
                        <button className="btn" onClick={addRFID} style={{ backgroundColor: "#28a745" }}>Add RFID</button>
                      </div>

                      {productData.rfidTags.length > 0 && (
                        <div style={{ backgroundColor: "#fff", padding: "10px", borderRadius: "5px" }}>
                          <strong>Added RFID Tags:</strong>
                          <div style={{ marginTop: "10px" }}>
                            {productData.rfidTags.map((tag, index) => (
                              <div
                                key={index}
                                style={{ display: "flex", justifyContent: "space-between", alignItems: "center", padding: "5px 10px", backgroundColor: "#e9ecef", borderRadius: "3px", marginBottom: "5px" }}
                              >
                                <span>{tag}</span>
                                <button
                                  onClick={() => removeRFID(index)}
                                  style={{ background: "#dc3545", color: "white", border: "none", borderRadius: "3px", padding: "2px 8px", cursor: "pointer" }}
                                >
                                  Remove
                                </button>
                              </div>
                            ))}
                          </div>
                        </div>
                      )}
                    </div>

                    <div style={{ display: "flex", gap: "10px", justifyContent: "space-between" }}>
                      <button className="btn cancel-btn" onClick={() => setWizardStep(1)}>← Back</button>
                      <button className="btn" onClick={() => setWizardStep(3)} style={{ backgroundColor: "#0d6efd" }}>Next →</button>
                    </div>
                  </div>
                )}

                {/* Step 3: Categories */}
                {wizardStep === 3 && (
                  <div>
                    <h3>Categories (Optional)</h3>
                    <div style={{ marginBottom: "20px" }}>
                      <div style={{ display: "flex", gap: "10px", marginBottom: "15px" }}>
                        <input
                          type="text"
                          placeholder="Category name"
                          value={productData.newCategory}
                          onChange={(e) => setProductData({ ...productData, newCategory: e.target.value })}
                          onKeyPress={(e) => e.key === "Enter" && addCategory()}
                          style={{ padding: "8px", fontSize: "14px", flex: 1 }}
                        />
                        <button className="btn" onClick={addCategory} style={{ backgroundColor: "#28a745" }}>Add Category</button>
                      </div>
                      {productData.categories.length > 0 && (
                        <div style={{ backgroundColor: "#fff", padding: "10px", borderRadius: "5px" }}>
                          <strong>Added Categories:</strong>
                          <div style={{ marginTop: "10px" }}>
                            {productData.categories.map((cat, index) => (
                              <div key={index} style={{ display: "flex", justifyContent: "space-between", alignItems: "center", padding: "5px 10px", backgroundColor: "#e9ecef", borderRadius: "3px", marginBottom: "5px" }}>
                                <span>{cat}</span>
                                <button onClick={() => removeCategory(index)} style={{ background: "#dc3545", color: "white", border: "none", borderRadius: "3px", padding: "2px 8px", cursor: "pointer" }}>Remove</button>
                              </div>
                            ))}
                          </div>
                        </div>
                      )}
                    </div>
                    <div style={{ display: "flex", gap: "10px", justifyContent: "space-between" }}>
                      <button className="btn cancel-btn" onClick={() => setWizardStep(2)}>← Back</button>
                      <button className="btn" onClick={() => setWizardStep(4)} style={{ backgroundColor: "#0d6efd" }}>Next →</button>
                    </div>
                  </div>
                )}

                {/* Step 4: Images */}
                {wizardStep === 4 && (
                  <div>
                    <h3>Product Images</h3>
                    <div style={{ marginBottom: "20px" }}>
                      {productData.images.map((img, index) => (
                        <div key={index} style={{ display: "flex", gap: "10px", marginBottom: "10px" }}>
                          <input
                            type="text"
                            placeholder="Size (e.g., small, medium, large)"
                            value={img.size}
                            onChange={(e) => updateImage(index, "size", e.target.value)}
                            style={{ padding: "8px", fontSize: "14px", flex: "0 0 200px" }}
                          />
                          <input
                            type="text"
                            placeholder="Image URL"
                            value={img.url}
                            onChange={(e) => updateImage(index, "url", e.target.value)}
                            style={{ padding: "8px", fontSize: "14px", flex: 1 }}
                          />
                          {productData.images.length > 1 && (
                            <button
                              onClick={() => removeImageField(index)}
                              style={{ background: "#dc3545", color: "white", border: "none", borderRadius: "3px", padding: "8px 15px", cursor: "pointer" }}
                            >
                              Remove
                            </button>
                          )}
                        </div>
                      ))}
                      <button className="btn" onClick={addImageField} style={{ backgroundColor: "#28a745", marginTop: "10px" }}>
                        + Add Another Image
                      </button>
                    </div>
                    <div style={{ display: "flex", gap: "10px", justifyContent: "space-between" }}>
                      <button className="btn cancel-btn" onClick={() => setWizardStep(3)}>← Back</button>
                      <div style={{ display: "flex", gap: "10px" }}>
                        <button className="btn cancel-btn" onClick={resetProductData}>Cancel</button>
                        <button className="btn" onClick={handleAddCompleteProduct} style={{ backgroundColor: "#28a745" }}>✓ Add Product to Database</button>
                      </div>
                    </div>
                  </div>
                )}
              </div>
            )}

            {/* VIEW PRODUCTS TAB */}
            {activeTab === "viewProducts" && (
              <div>
                <h3>Products Database</h3>
                {products.length === 0 ? (
                  <p>No products found or loading...</p>
                ) : (
                  <div style={{ overflowX: "auto" }}>
                    <table className="products-table">
                      <thead>
                        <tr>
                          <th>UPC</th>
                          <th>RFID Tags</th>
                          <th>Brand</th>
                          <th>Description</th>
                          <th>Qty</th>
                          <th>Price</th>
                          <th>Weight</th>
                          <th>Produce</th>
                          <th>Categories</th>
                          <th>Aisle Locations</th>
                          <th>Images</th>
                          <th>Action</th>
                        </tr>
                      </thead>
                      <tbody>
                        {products.map((product) => (
                          <tr key={product.upc}>
                            <td>{product.upc}</td>
                            <td>
                              {product.rfidTags.length
                                ? product.rfidTags.join(", ")
                                : ""}
                            </td>
                            <td>{product.brand || "N/A"}</td>
                            <td>{product.description}</td>
                            <td>{product.qty}</td>
                            <td>${Number(product.price).toFixed(2)}</td>
                            <td>{product.weight}</td>
                            <td>{product.produce}</td>
                            <td>
                              {product.categories.length ? product.categories.join(", ") : "N/A"}
                            </td>
                            <td>
                              {product.aisleLocations.length ? (
                                <table style={{ width: "100%", borderCollapse: "collapse" }}>
                                  <thead>
                                    <tr>
                                      <th>Aisle</th>
                                      <th>Section</th>
                                      <th>Side</th>
                                      <th>Description</th>
                                    </tr>
                                  </thead>
                                  <tbody>
                                    {product.aisleLocations.map((loc, idx) => (
                                      <tr key={idx}>
                                        <td>{loc.aisle}</td>
                                        <td>{loc.section}</td>
                                        <td>{loc.side}</td>
                                        <td>{loc.description}</td>
                                      </tr>
                                    ))}
                                  </tbody>
                                </table>
                              ) : (
                                "N/A"
                              )}
                            </td>
                            <td>
                              {product.images.length ? (
                                <div style={{ display: "flex", flexWrap: "wrap", gap: "5px" }}>
                                  {product.images.map((img, idx) => (
                                    <div
                                      key={idx}
                                      style={{
                                        position: "relative",
                                        display: "inline-block",
                                        width: "60px",
                                        height: "60px",
                                        cursor: "pointer",
                                      }}
                                      className="img-container"
                                    >
                                      <img
                                        src={img.url}
                                        alt={`${product.upc}-${img.size}`}
                                        style={{
                                          width: "100%",
                                          height: "100%",
                                          objectFit: "cover",
                                          borderRadius: "4px",
                                        }}
                                      />
                                    </div>
                                  ))}
                                </div>
                              ) : (
                                "N/A"
                              )}
                            </td>
                            <td>
                              <button
                                className="btn"
                                onClick={() => {
                                  setEditingProduct(product.upc);
                                  setEditFormData({ ...product });
                                }}
                                style={{
                                  backgroundColor: "#ffc107",
                                  padding: "4px 12px",
                                  fontSize: "12px",
                                }}
                              >
                                Edit
                              </button>
                            </td>
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  </div>
                )}
              </div>
            )}
          </div>
        )}

        {/* Misc Section */}
        {activeSection === "misc" && (
          <div>
            <h2 style={{ marginBottom: "20px" }}>Misc Information</h2>

            <p>Measured Weight: {verificationInfo.measuredWeight} oz</p>
            <p>Expected Cart Weight: {verificationInfo.expectedWeight} oz</p>
            <p>Weight Difference: {verificationInfo.weightDifference} oz</p>
            <p>Scanned RFID Tags: {verificationInfo.scannedTags.join(", ") || "None"}</p>
            <p>RFID UPCs in Cart: {verificationInfo.rfidUpcs.join(", ") || "None"}</p>
          </div>
        )}

        {/* Setup Info Section */}
        {activeSection === "setup" && (
          <div>
            <h2 style={{ marginBottom: "20px" }}>Setup Information</h2>

            <div
              style={{
                backgroundColor: "white",
                padding: "25px",
                borderRadius: "8px",
                boxShadow: "0 2px 4px rgba(0,0,0,0.1)",
              }}
            >
              <div style={{ marginBottom: "15px" }}>
                <label style={{ display: "block", marginBottom: "5px" }}>Master Server IP:</label>
                <input
                  type="text"
                  value={masterIp}
                  onChange={(e) => setMasterIp(e.target.value)}
                  style={{ width: "100%", padding: "8px", borderRadius: "5px", border: "1px solid #ccc" }}
                />
              </div>

              <div style={{ marginBottom: "15px" }}>
                <label style={{ display: "block", marginBottom: "5px" }}>Master Server Port:</label>
                <input
                  type="text"
                  value={masterPort}
                  onChange={(e) => setMasterPort(e.target.value)}
                  style={{ width: "100%", padding: "8px", borderRadius: "5px", border: "1px solid #ccc" }}
                />
              </div>

              <div style={{ marginBottom: "15px" }}>
                <label style={{ display: "block", marginBottom: "5px" }}>Company IP (Carte Diem):</label>
                <input
                  type="text"
                  value={companyIp}
                  onChange={(e) => setCompanyIp(e.target.value)}
                  style={{ width: "100%", padding: "8px", borderRadius: "5px", border: "1px solid #ccc" }}
                />
              </div>

              <div style={{ marginBottom: "20px" }}>
                <label style={{ display: "block", marginBottom: "5px" }}>Company Port (Carte Diem):</label>
                <input
                  type="text"
                  value={companyPort}
                  onChange={(e) => setCompanyPort(e.target.value)}
                  style={{ width: "100%", padding: "8px", borderRadius: "5px", border: "1px solid #ccc" }}
                />
              </div>

              <button
                onClick={handleSave}
                style={{
                  backgroundColor: "#3498db",
                  color: "white",
                  padding: "12px 24px",
                  border: "none",
                  borderRadius: "5px",
                  cursor: "pointer",
                  fontSize: "16px",
                }}
              >
                Save Setup
              </button>
            </div>
          </div>
        )}
      </div>

      {/* Reset Confirmation Modal */}
      {confirmReset && (
        <div className="modal-overlay">
          <div
            className="modal"
            style={{
              minWidth: "400px",
              textAlign: "center",
              backgroundColor: "white",
              padding: "30px",
              borderRadius: "8px",
            }}
          >
            <h3 style={{ marginBottom: "15px" }}>⚠️ Confirm Reset Cart</h3>
            <p style={{ margin: "20px 0", color: "#666", lineHeight: "1.5" }}>
              Are you sure you want to reset the cart? All items will be removed. This action
              cannot be undone.
            </p>
            <div
              style={{
                marginTop: "25px",
                display: "flex",
                justifyContent: "center",
                gap: "15px",
              }}
            >
              <button
                className="btn"
                style={{
                  backgroundColor: "#dc3545",
                  color: "white",
                  padding: "12px 24px",
                  border: "none",
                  borderRadius: "5px",
                  cursor: "pointer",
                  fontSize: "16px",
                }}
                onClick={handleResetCart}
              >
                Yes, Reset Cart
              </button>
              <button
                className="btn cancel-btn"
                onClick={() => setConfirmReset(false)}
                style={{
                  padding: "12px 24px",
                  backgroundColor: "#6c757d",
                  color: "white",
                  border: "none",
                  borderRadius: "5px",
                  cursor: "pointer",
                  fontSize: "16px",
                }}
              >
                Cancel
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Edit Product Modal */}
      {editingProduct && (
        <div className="modal-overlay">
          <div
            className="modal"
            style={{ minWidth: "500px", maxHeight: "80vh", overflowY: "auto" }}
          >
            <h2>Edit Product - {editFormData.upc}</h2>

            <div>
              <h3>Product Information</h3>
              <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "10px" }}>
                <div>
                  <label
                    style={{
                      display: "block",
                      marginBottom: "5px",
                      fontWeight: "bold",
                    }}
                  >
                    UPC
                  </label>
                  <input type="text" value={editFormData.upc} disabled style={{ width: "100%" }} />
                </div>
                <div>
                  <label
                    style={{
                      display: "block",
                      marginBottom: "5px",
                      fontWeight: "bold",
                    }}
                  >
                    Brand
                  </label>
                  <input
                    type="text"
                    value={editFormData.brand || ""}
                    onChange={(e) =>
                      setEditFormData({
                        ...editFormData,
                        brand: e.target.value,
                      })
                    }
                    style={{ width: "100%" }}
                  />
                </div>
                <div style={{ gridColumn: "1 / -1" }}>
                  <label
                    style={{
                      display: "block",
                      marginBottom: "5px",
                      fontWeight: "bold",
                    }}
                  >
                    Description
                  </label>
                  <input
                    type="text"
                    value={editFormData.description}
                    onChange={(e) =>
                      setEditFormData({
                        ...editFormData,
                        description: e.target.value,
                      })
                    }
                    style={{ width: "100%" }}
                  />
                </div>
                <div>
                  <label
                    style={{
                      display: "block",
                      marginBottom: "5px",
                      fontWeight: "bold",
                    }}
                  >
                    Quantity
                  </label>
                  <input
                    type="number"
                    value={editFormData.qty}
                    onChange={(e) => setEditFormData({ ...editFormData, qty: e.target.value })}
                    style={{ width: "100%" }}
                  />
                </div>
                <div>
                  <label
                    style={{
                      display: "block",
                      marginBottom: "5px",
                      fontWeight: "bold",
                    }}
                  >
                    Price
                  </label>
                  <input
                    type="number"
                    step="0.01"
                    value={editFormData.price}
                    onChange={(e) =>
                      setEditFormData({
                        ...editFormData,
                        price: e.target.value,
                      })
                    }
                    style={{ width: "100%" }}
                  />
                </div>
                <div>
                  <label
                    style={{
                      display: "block",
                      marginBottom: "5px",
                      fontWeight: "bold",
                    }}
                  >
                    Weight
                  </label>
                  <input
                    type="number"
                    step="0.01"
                    value={editFormData.weight}
                    onChange={(e) =>
                      setEditFormData({
                        ...editFormData,
                        weight: e.target.value,
                      })
                    }
                    style={{ width: "100%" }}
                  />
                </div>
                <div>
                  <label
                    style={{
                      display: "block",
                      marginBottom: "5px",
                      fontWeight: "bold",
                    }}
                  >
                    Produce
                  </label>
                  <input
                    type="number"
                    value={editFormData.produce}
                    onChange={(e) =>
                      setEditFormData({
                        ...editFormData,
                        produce: e.target.value,
                      })
                    }
                    style={{ width: "100%" }}
                  />
                </div>
                
                {/* RFID Tags */}
                <div style={{ gridColumn: "1 / -1", marginTop: "20px" }}>
                  <h3>RFID Tags</h3>
                  {(editFormData.rfidTags || []).map((tag, index) => (
                    <div
                      key={index}
                      style={{ display: "flex", gap: "10px", marginBottom: "5px", alignItems: "center" }}
                    >
                      <input
                        type="text"
                        value={tag}
                        placeholder="Enter RFID Tag"
                        onChange={(e) => {
                          const newTags = [...(editFormData.rfidTags || [])];
                          newTags[index] = e.target.value;
                          setEditFormData({ ...editFormData, rfidTags: newTags });
                        }}
                        style={{ flex: 1, padding: "8px" }}
                      />
                      {editFormData.rfidTags.length > 1 && (
                        <button
                          onClick={() =>
                            setEditFormData({
                              ...editFormData,
                              rfidTags: editFormData.rfidTags.filter((_, i) => i !== index),
                            })
                          }
                          style={{
                            background: "#dc3545",
                            color: "white",
                            border: "none",
                            borderRadius: "3px",
                            padding: "8px 15px",
                            cursor: "pointer",
                          }}
                        >
                          Remove
                        </button>
                      )}
                    </div>
                  ))}
                  <button
                    onClick={() =>
                      setEditFormData({
                        ...editFormData,
                        rfidTags: [...(editFormData.rfidTags || []), ""],
                      })
                    }
                    style={{
                      marginTop: "10px",
                      backgroundColor: "#28a745",
                      color: "white",
                      border: "none",
                      borderRadius: "3px",
                      padding: "8px 15px",
                      cursor: "pointer",
                    }}
                  >
                    + Add RFID Tag
                  </button>
                </div>

                {/* Aisle Location */}
                <div style={{ gridColumn: "1 / -1", marginTop: "20px" }}>
                  <h3>Aisle Location</h3>
                  <div
                    style={{
                      display: "grid",
                      gridTemplateColumns: "1fr 1fr 1fr 2fr",
                      gap: "10px",
                      marginBottom: "10px",
                    }}
                  >
                    <input
                      type="text"
                      placeholder="Aisle"
                      value={editFormData.aisleLocations?.[0]?.aisle || ""}
                      onChange={(e) => {
                        setEditFormData({
                          ...editFormData,
                          aisleLocations: [
                            {
                              ...editFormData.aisleLocations?.[0],
                              aisle: e.target.value,
                            },
                          ],
                        });
                      }}
                    />
                    <input
                      type="text"
                      placeholder="Section"
                      value={editFormData.aisleLocations?.[0]?.section || ""}
                      onChange={(e) => {
                        setEditFormData({
                          ...editFormData,
                          aisleLocations: [
                            {
                              ...editFormData.aisleLocations?.[0],
                              section: e.target.value,
                            },
                          ],
                        });
                      }}
                    />
                    <input
                      type="text"
                      placeholder="Side"
                      value={editFormData.aisleLocations?.[0]?.side || ""}
                      onChange={(e) => {
                        setEditFormData({
                          ...editFormData,
                          aisleLocations: [
                            {
                              ...editFormData.aisleLocations?.[0],
                              side: e.target.value,
                            },
                          ],
                        });
                      }}
                    />
                    <input
                      type="text"
                      placeholder="Description"
                      value={editFormData.aisleLocations?.[0]?.description || ""}
                      onChange={(e) => {
                        setEditFormData({
                          ...editFormData,
                          aisleLocations: [
                            {
                              ...editFormData.aisleLocations?.[0],
                              description: e.target.value,
                            },
                          ],
                        });
                      }}
                    />
                  </div>
                </div>

                {/* Categories */}
                <div style={{ gridColumn: "1 / -1", marginTop: "20px" }}>
                  <h3>Categories</h3>
                  {(editFormData.categories || []).map((cat, index) => (
                    <div
                      key={index}
                      style={{
                        display: "flex",
                        gap: "10px",
                        marginBottom: "5px",
                        alignItems: "center",
                      }}
                    >
                      <input
                        type="text"
                        value={cat}
                        onChange={(e) => {
                          const newCats = [...editFormData.categories];
                          newCats[index] = e.target.value;
                          setEditFormData({ ...editFormData, categories: newCats });
                        }}
                        style={{ flex: 1, padding: "8px" }}
                      />
                      {editFormData.categories.length > 1 && (
                        <button
                          onClick={() => {
                            const newCats = editFormData.categories.filter((_, i) => i !== index);
                            setEditFormData({ ...editFormData, categories: newCats });
                          }}
                          style={{
                            background: "#dc3545",
                            color: "white",
                            border: "none",
                            borderRadius: "3px",
                            padding: "8px 15px",
                            cursor: "pointer",
                          }}
                        >
                          Remove
                        </button>
                      )}
                    </div>
                  ))}
                  <button
                    onClick={() =>
                      setEditFormData({
                        ...editFormData,
                        categories: [...(editFormData.categories || []), ""],
                      })
                    }
                    style={{
                      marginTop: "10px",
                      backgroundColor: "#28a745",
                      color: "white",
                      border: "none",
                      borderRadius: "3px",
                      padding: "8px 15px",
                      cursor: "pointer",
                    }}
                  >
                    + Add Category
                  </button>
                </div>

                <div style={{ gridColumn: "1 / -1", marginTop: "20px" }}>
                  <h3>Product Images</h3>
                  {editFormData.images && editFormData.images.length > 0 ? (
                    editFormData.images.map((img, index) => (
                      <div
                        key={index}
                        style={{
                          display: "flex",
                          gap: "10px",
                          marginBottom: "10px",
                          alignItems: "center",
                        }}
                      >
                        <input
                          type="text"
                          placeholder="Size (e.g., small, medium, large)"
                          value={img.size}
                          onChange={(e) => {
                            const newImages = [...editFormData.images];
                            newImages[index].size = e.target.value;
                            setEditFormData({
                              ...editFormData,
                              images: newImages,
                            });
                          }}
                          style={{
                            padding: "8px",
                            fontSize: "14px",
                            flex: "0 0 150px",
                          }}
                        />
                        <input
                          type="text"
                          placeholder="Image URL"
                          value={img.url}
                          onChange={(e) => {
                            const newImages = [...editFormData.images];
                            newImages[index].url = e.target.value;
                            setEditFormData({
                              ...editFormData,
                              images: newImages,
                            });
                          }}
                          style={{ padding: "8px", fontSize: "14px", flex: 1 }}
                        />
                        {editFormData.images.length > 1 && (
                          <button
                            onClick={() => {
                              const newImages = editFormData.images.filter((_, i) => i !== index);
                              setEditFormData({
                                ...editFormData,
                                images: newImages,
                              });
                            }}
                            style={{
                              background: "#dc3545",
                              color: "white",
                              border: "none",
                              borderRadius: "3px",
                              padding: "8px 15px",
                              cursor: "pointer",
                            }}
                          >
                            Remove
                          </button>
                        )}
                      </div>
                    ))
                  ) : (
                    <p>No images yet.</p>
                  )}
                  <button
                    onClick={() => {
                      const newImages = [...(editFormData.images || []), { size: "", url: "" }];
                      setEditFormData({ ...editFormData, images: newImages });
                    }}
                    style={{
                      marginTop: "10px",
                      backgroundColor: "#28a745",
                      color: "white",
                      border: "none",
                      borderRadius: "3px",
                      padding: "8px 15px",
                      cursor: "pointer",
                    }}
                  >
                    + Add Another Image
                  </button>
                </div>
              </div>
            </div>

            <div
              style={{
                marginTop: "20px",
                display: "flex",
                gap: "10px",
                justifyContent: "space-between",
              }}
            >
              <button
                onClick={async () => {
                  try {
                    setStatusMessage("Updating product...");
                    setStatusType("success");

                    const data = await window.electronAPI.runPython("cart_ops.py", [
                      "update_product",
                      JSON.stringify(editFormData),
                    ]);

                    if (data.status === "success") {
                      setProducts((prevProducts) =>
                        prevProducts.map((p) => (p.upc === editFormData.upc ? data.product : p))
                      );

                      setStatusMessage(`Product ${editFormData.upc} has been updated.`);
                      setStatusType("success");
                      setEditingProduct(null);
                    } else {
                      throw new Error(data.message);
                    }
                  } catch (err) {
                    console.error(err);
                    setStatusMessage(`Error updating Product ${editFormData.upc}: ${err.message}`);
                    setStatusType("error");
                  }
                }}
                className="btn"
                style={{ backgroundColor: "#28a745", color: "white" }}
              >
                Save Changes
              </button>

              <button className="btn cancel-btn" onClick={() => setEditingProduct(null)}>
                Cancel
              </button>

              <button
                className="btn"
                style={{ backgroundColor: "red", color: "white" }}
                onClick={() => {
                  setUpcToDelete(editFormData.upc);
                  setDeleteConfirmOpen(true);
                }}
              >
                Delete
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Delete Confirmation Modal */}
      {deleteConfirmOpen && (
        <div className="modal-overlay">
          <div className="modal" style={{ minWidth: "400px", textAlign: "center" }}>
            <h3>Confirm Delete</h3>
            <p>Are you sure you want to delete product {upcToDelete}?</p>
            <div
              style={{
                marginTop: "20px",
                display: "flex",
                justifyContent: "center",
                gap: "10px",
              }}
            >
              <button
                className="btn"
                style={{
                  backgroundColor: "red",
                  color: "white",
                  padding: "6px 12px",
                }}
                onClick={async () => {
                  if (!upcToDelete) return;

                  const deleteQuery = `
                        BEGIN TRANSACTION;

                        DELETE FROM Products WHERE upc='${upcToDelete}';
                        DELETE FROM AisleLocations WHERE upc='${upcToDelete}';
                        DELETE FROM ProductCategories WHERE upc='${upcToDelete}';
                        DELETE FROM Images WHERE upc='${upcToDelete}';
                        DELETE FROM ProductRFIDs WHERE upc='${upcToDelete}';

                        COMMIT;
                      `;

                  try {
                    const data = await window.electronAPI.runPython("cart_ops.py", [
                      "execute_sql",
                      deleteQuery,
                    ]);
                    if (data.status === "error") throw new Error(data.message);

                    setProducts((prev) => prev.filter((p) => p.upc !== upcToDelete));
                    setStatusMessage(`Product ${upcToDelete} and related data deleted.`);
                    setStatusType("success");
                  } catch (err) {
                    console.error(err);
                    setStatusMessage(`Error deleting product ${upcToDelete}.`);
                    setStatusType("error");
                  } finally {
                    setDeleteConfirmOpen(false);
                    setUpcToDelete(null);
                    setEditingProduct(null);
                  }
                }}
              >
                Yes, Delete
              </button>
              <button
                className="btn cancel-btn"
                onClick={() => {
                  setDeleteConfirmOpen(false);
                  setUpcToDelete(null);
                }}
                style={{ padding: "6px 12px" }}
              >
                Cancel
              </button>
            </div>
          </div>
        </div>
      )}

      {queryResult && (
        <div
          className="query-result"
          style={{
            position: "fixed",
            bottom: "20px",
            right: "20px",
            maxWidth: "400px",
            padding: "15px",
            backgroundColor: "#f8f9fa",
            borderRadius: "5px",
            boxShadow: "0 4px 6px rgba(0,0,0,0.1)",
            zIndex: 1000,
          }}
        >
          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
            <h4 style={{ margin: 0 }}>Result:</h4>
            <button
              onClick={() => setQueryResult("")}
              style={{
                background: "transparent",
                border: "none",
                fontSize: "20px",
                cursor: "pointer",
              }}
            >
              ×
            </button>
          </div>
          <pre
            style={{
              whiteSpace: "pre-wrap",
              wordBreak: "break-word",
              fontSize: "12px",
              maxHeight: "200px",
              overflowY: "auto",
              marginTop: "10px",
            }}
          >
            {queryResult}
          </pre>
        </div>
      )}
    </div>
  );
};

export default Admin;