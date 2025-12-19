import { isTauri } from "@tauri-apps/api/core";
import type { Settings } from "./settings";
import type { ScheduleType } from "./types";

export const OVERLAY_WINDOW_LABEL = "overlay";

export async function ensureOverlayWindow(): Promise<void> {
  if (!isTauri()) return;
  try {
    const { LogicalPosition, LogicalSize, WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const existing = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (existing) return;

    const win = new WebviewWindow(OVERLAY_WINDOW_LABEL, {
      title: "helltime overlay",
      url: "/?view=overlay",
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

export type OverlayWindowSettings = {
  enabled: boolean;
  mode: "overview" | "toast";
  categories: Record<ScheduleType, boolean>;
  bgHex: string;
  bgOpacity: number;
  scale: number;
};

export function toOverlayWindowSettings(settings: Settings): OverlayWindowSettings {
  return {
    enabled: settings.overlayWindowEnabled,
    mode: settings.overlayWindowMode,
    categories: settings.overlayWindowCategories,
    bgHex: settings.overlayBgHex,
    bgOpacity: settings.overlayBgOpacity,
    scale: settings.overlayScale
  };
}

export async function broadcastOverlayWindowSettings(settings: Settings): Promise<void> {
  if (!isTauri()) return;
  try {
    const { emit, emitTo } = await import("@tauri-apps/api/event");
    const payload = toOverlayWindowSettings(settings);
    try {
      await emitTo(OVERLAY_WINDOW_LABEL, "helltime:overlay-settings", payload);
    } catch {
      await emit("helltime:overlay-settings", payload);
    }
  } catch (e) {
    // eslint-disable-next-line no-console
    console.warn("broadcastOverlayWindowSettings failed", e);
  }
}
