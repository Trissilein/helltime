import React from "react";
import ReactDOM from "react-dom/client";
import App from "./App";
import OverviewOverlay from "./OverviewOverlay";
import OverlayWindow from "./OverlayWindow";
import { ErrorBoundary } from "./ErrorBoundary";
import { enablePanicStop, startUiWatchdog } from "./lib/safety";
import { initMainWindowPersistence, initWindowPersistence } from "./lib/window_state";
import "./styles.css";

const view = new URLSearchParams(window.location.search).get("view");
if (view !== "overview" && view !== "overlay") {
  window.addEventListener("error", (e) => {
    void enablePanicStop(e.error ?? e.message);
  });

  window.addEventListener("unhandledrejection", (e) => {
    void enablePanicStop((e as PromiseRejectionEvent).reason);
  });
}

if (view === "overview") {
  void initWindowPersistence("helltime:overviewWindowBounds");
} else if (view === "overlay") {
  void initWindowPersistence("helltime:overlayWindowBounds");
} else {
  startUiWatchdog();
  void initMainWindowPersistence();
}

ReactDOM.createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <ErrorBoundary>
      {view === "overview" ? <OverviewOverlay /> : view === "overlay" ? <OverlayWindow /> : <App />}
    </ErrorBoundary>
  </React.StrictMode>
);
