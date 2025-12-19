import { isTauri } from "@tauri-apps/api/core";

export const OVERLAY_WINDOW_LABEL = "overlay";

export async function ensureOverlayWindow(): Promise<void> {
  if (!isTauri()) return;
  try {
    const { LogicalPosition, LogicalSize, WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const existing = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (existing) return;

    const win = new WebviewWindow(OVERLAY_WINDOW_LABEL, {
      title: "helltime overlay",
      url: "index.html?view=overlay",
      center: true,
      x: 40,
      y: 40,
      width: 340,
      height: 160,
      minWidth: 260,
      minHeight: 120,
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
    });

    // Best-effort to keep it reachable even if persistence is borked.
    try {
      await win.setSize(new LogicalSize(340, 160));
      await win.setPosition(new LogicalPosition(40, 40));
    } catch {
      // ignore
    }
  } catch (e) {
    // eslint-disable-next-line no-console
    console.warn("ensureOverlayWindow failed", e);
  }
}

export async function setOverlayWindowVisible(visible: boolean): Promise<void> {
  if (!isTauri()) return;
  try {
    const { WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const win = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (!win) {
      if (visible) await ensureOverlayWindow();
      return;
    }
    if (visible) {
      await win.show();
      await win.setAlwaysOnTop(true);
    } else {
      await win.hide();
    }
  } catch (e) {
    // eslint-disable-next-line no-console
    console.warn("setOverlayWindowVisible failed", e);
  }
}

