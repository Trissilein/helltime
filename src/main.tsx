import React from "react";
import ReactDOM from "react-dom/client";
import { isTauri } from "@tauri-apps/api/core";
import App from "./App";
import OverlayWindow from "./OverlayWindow";
import { ErrorBoundary } from "./ErrorBoundary";
import { enablePanicStop, startUiWatchdog } from "./lib/safety";
import { initMainWindowPersistence, initWindowPersistence } from "./lib/window_state";
import "./styles.css";

const view = new URLSearchParams(window.location.search).get("view");
if (view !== "overlay") {
  window.addEventListener("error", (e) => {
    void enablePanicStop(e.error ?? e.message);
  });

  window.addEventListener("unhandledrejection", (e) => {
    void enablePanicStop((e as PromiseRejectionEvent).reason);
  });
}

if (view !== "overlay" && isTauri()) {
  void (async () => {
    try {
      const { getCurrentWindow } = await import("@tauri-apps/api/window");
      const { WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
      const main = getCurrentWindow();
      await main.onCloseRequested(async () => {
        try {
          const overlay = await WebviewWindow.getByLabel("overlay");
          await overlay?.destroy();
        } catch {
          // ignore
        }
      });
    } catch {
      // ignore
    }
  })();
}

if (view === "overlay") {
  void initWindowPersistence("helltime:overlayWindowBounds");
} else {
  startUiWatchdog();
  void initMainWindowPersistence();
}

ReactDOM.createRoot(document.getElementById("root")!).render(
  <ErrorBoundary>{view === "overlay" ? <OverlayWindow /> : <App />}</ErrorBoundary>
);
