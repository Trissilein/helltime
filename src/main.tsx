import React from "react";
import ReactDOM from "react-dom/client";
import App from "./App";
import { ErrorBoundary } from "./ErrorBoundary";
import { enablePanicStop, startUiWatchdog } from "./lib/safety";
import "./styles.css";

window.addEventListener("error", (e) => {
  void enablePanicStop(e.error ?? e.message);
});

window.addEventListener("unhandledrejection", (e) => {
  void enablePanicStop((e as PromiseRejectionEvent).reason);
});

startUiWatchdog();

ReactDOM.createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <ErrorBoundary>
      <App />
    </ErrorBoundary>
  </React.StrictMode>
);
