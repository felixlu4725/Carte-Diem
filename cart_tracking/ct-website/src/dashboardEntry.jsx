import React from 'react';
import ReactDOM from 'react-dom/client';
import Dashboard from './Dashboard.jsx';
import './index.css';

const serverUrl = window.location.origin;

ReactDOM.createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <Dashboard 
      serverUrl={serverUrl} 
      // onLogout={() => {
      //   window.location.href = `http://localhost:${originPort}`;
      // }} 
    />
  </React.StrictMode>,
);