import { isTauri } from "@tauri-apps/api/core";

const OVERVIEW_LABEL = "overview";
let toggleInFlight: Promise<void> | null = null;

export async function setOverviewOverlayEnabled(enabled: boolean): Promise<void> {
  if (!isTauri()) return;
  if (toggleInFlight) {
    await toggleInFlight;
  }
  try {
    const { LogicalPosition, LogicalSize, WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const existing = await WebviewWindow.getByLabel(OVERVIEW_LABEL);

    if (enabled) {
      if (existing) {
        await existing.show();
        await existing.setAlwaysOnTop(true);
        return;
      }

      toggleInFlight = (async () => {
        const win = new WebviewWindow(OVERVIEW_LABEL, {
          title: "helltime overview",
          // Use index.html to work with both devUrl and frontendDist.
          url: "index.html?view=overview",
          center: true,
          x: 40,
          y: 40,
          width: 320,
          height: 140,
          minWidth: 260,
          minHeight: 110,
          decorations: false,
          alwaysOnTop: true,
          skipTaskbar: true,
          resizable: true,
          focus: false,
          focusable: false,
          transparent: false,
          visible: true
        });

        win.once("tauri://created", () => {
          void win.setAlwaysOnTop(true);
          void win.show();
          window.setTimeout(() => void win.setAlwaysOnTop(true), 250);
        });

        win.once("tauri://error", (e) => {
          // eslint-disable-next-line no-console
          console.warn("overview overlay window error", e);
        });

        // If the window was created off-screen somehow, best-effort place it.
        try {
          await win.setSize(new LogicalSize(320, 140));
          await win.setPosition(new LogicalPosition(40, 40));
        } catch {
          // ignore
        }
      })();
      await toggleInFlight;
      toggleInFlight = null;
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

export async function resetOverviewOverlayWindow(): Promise<void> {
  try {
    localStorage.removeItem("helltime:overviewWindowBounds");
  } catch {
    // ignore
  }
  if (!isTauri()) return;
  try {
    const { LogicalPosition, LogicalSize, WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const win = await WebviewWindow.getByLabel(OVERVIEW_LABEL);
    if (!win) {
      await setOverviewOverlayEnabled(true);
      return;
    }
    await win.setSize(new LogicalSize(320, 140));
    await win.setPosition(new LogicalPosition(40, 40));
    await win.show();
    await win.setAlwaysOnTop(true);
  } catch (e) {
    // eslint-disable-next-line no-console
    console.warn("resetOverviewOverlayWindow failed", e);
  }
}
