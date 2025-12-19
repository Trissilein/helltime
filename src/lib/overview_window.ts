import { isTauri } from "@tauri-apps/api/core";

const OVERVIEW_LABEL = "overview";

export async function setOverviewOverlayEnabled(enabled: boolean): Promise<void> {
  if (!isTauri()) return;
  try {
    const { WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const existing = await WebviewWindow.getByLabel(OVERVIEW_LABEL);

    if (enabled) {
      if (existing) {
        await existing.show();
        await existing.setAlwaysOnTop(true);
        return;
      }

      const win = new WebviewWindow(OVERVIEW_LABEL, {
        title: "helltime overlay",
        url: "/?view=overview",
        width: 320,
        height: 140,
        minWidth: 260,
        minHeight: 110,
        decorations: false,
        alwaysOnTop: true,
        skipTaskbar: true,
        resizable: true,
        focus: false,
        transparent: false,
        visible: true
      });

      win.once("tauri://error", (e) => {
        // eslint-disable-next-line no-console
        console.warn("overview overlay window error", e);
      });

      return;
    }

    if (existing) {
      await existing.hide();
    }
  } catch (e) {
    // eslint-disable-next-line no-console
    console.warn("setOverviewOverlayEnabled failed", e);
  }
}

