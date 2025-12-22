import { useState } from "react";
import Account from "./Account.jsx";
import "./App.css";

function App() {
  const [accountAction, setAccountAction] = useState(""); // "login" or "create"
  const [page, setPage] = useState("home"); // "home" or "account"

  const goToDashboard = (url) => {
    const originPort = window.location.port;
    const hasQuery = url.includes("?");

    const redirectUrl = `${url}${hasQuery ? "&" : "?"}originPort=${originPort}`;
    window.location.href = redirectUrl;
  };

  return (
    <div className="app-container">
      {page === "home" && (
        <div>
          <h1 className="title">Carte Diem</h1>
          <h2 className="subtitle">Cart Tracking System</h2>

          <button
            onClick={() => {
              setAccountAction("create");
              setPage("account");
            }}
            className="create-button"
          >
            Create Account
          </button>

          <button
            onClick={() => {
              setAccountAction("login");
              setPage("account");
            }}
            className="login-button"
          >
            Login
          </button>
        </div>
      )}

      {page === "account" && (
        <div>
          <Account
            action={accountAction}
            goBack={() => setPage("home")}
            goToDashboard={goToDashboard}
          />
        </div>
      )}
    </div>
  );
}

export default App;
