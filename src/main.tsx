import React from "react";
import ReactDOM from "react-dom/client";
import App from "./App";
import OverviewOverlay from "./OverviewOverlay";
import { ErrorBoundary } from "./ErrorBoundary";
import { enablePanicStop, startUiWatchdog } from "./lib/safety";
import { initMainWindowPersistence, initWindowPersistence } from "./lib/window_state";
import "./styles.css";

window.addEventListener("error", (e) => {
  void enablePanicStop(e.error ?? e.message);
});

window.addEventListener("unhandledrejection", (e) => {
  void enablePanicStop((e as PromiseRejectionEvent).reason);
});

startUiWatchdog();

const view = new URLSearchParams(window.location.search).get("view");
if (view === "overview") {
  void initWindowPersistence("helltime:overviewWindowBounds");
} else {
  void initMainWindowPersistence();
}

ReactDOM.createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <ErrorBoundary>
      {view === "overview" ? <OverviewOverlay /> : <App />}
    </ErrorBoundary>
  </React.StrictMode>
);
