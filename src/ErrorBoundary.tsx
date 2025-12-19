import React from "react";
import { enablePanicStop } from "./lib/safety";

export class ErrorBoundary extends React.Component<React.PropsWithChildren, { failed: boolean }> {
  state = { failed: false };

  static getDerivedStateFromError() {
    return { failed: true };
  }

  componentDidCatch(error: unknown) {
    void enablePanicStop(error);
  }

  render() {
    if (this.state.failed) {
      return (
        <div style={{ padding: 16 }}>
          <div style={{ fontWeight: 800, marginBottom: 8 }}>UI Error</div>
          <div style={{ opacity: 0.8, fontSize: 12, lineHeight: 1.35 }}>
            Sicherheits-Stopp aktiv. Overlay/Ton wurden deaktiviert.
          </div>
          <div style={{ marginTop: 12 }}>
            <button className="btn" type="button" onClick={() => window.location.reload()}>
              Reload
            </button>
          </div>
        </div>
      );
    }
    return this.props.children;
  }
}

