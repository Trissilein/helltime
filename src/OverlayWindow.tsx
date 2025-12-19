import React, { useEffect } from "react";

export default function OverlayWindow() {
  useEffect(() => {
    document.body.classList.add("overviewMode");
    return () => document.body.classList.remove("overviewMode");
  }, []);

  return (
    <div className="container overlayOverview" data-tauri-drag-region>
      <div className="overlayOverviewHeader" data-tauri-drag-region>
        <div className="overlayOverviewBrand" data-tauri-drag-region>
          helltime overlay
        </div>
        <div className="overlayOverviewMeta" data-tauri-drag-region>
          —
        </div>
      </div>
      <div className="overlayOverviewError" style={{ marginTop: 10 }}>
        Overlay scaffold (noch nicht aktiv)
      </div>
    </div>
  );
}

