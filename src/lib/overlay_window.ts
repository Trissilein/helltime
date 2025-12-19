import { isTauri } from "@tauri-apps/api/core";
import type { Settings } from "./settings";
import type { ScheduleType } from "./types";
import { pushOverlayDiag } from "./overlay_diag";

export const OVERLAY_WINDOW_LABEL = "overlay";

export async function ensureOverlayWindow(): Promise<void> {
  if (!isTauri()) return;
  pushOverlayDiag("ensureOverlayWindow()");
  try {
    const { LogicalPosition, LogicalSize, WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const existing = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (existing) {
      pushOverlayDiag("ensureOverlayWindow: already exists");
      return;
    }

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
      pushOverlayDiag("overlay window created");
      void win.setAlwaysOnTop(true);
      void win.show();
    });
    win.once("tauri://error", (e) => {
      pushOverlayDiag(`overlay window error: ${String((e as any)?.payload ?? e)}`);
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
    pushOverlayDiag(`ensureOverlayWindow failed: ${String(e)}`);
  }
}

export async function setOverlayWindowVisible(visible: boolean): Promise<void> {
  if (!isTauri()) return;
  pushOverlayDiag(`setOverlayWindowVisible(${visible})`);
  try {
    const { WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const win = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (!win) {
      pushOverlayDiag("setOverlayWindowVisible: no window");
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
    pushOverlayDiag(`setOverlayWindowVisible failed: ${String(e)}`);
  }
}

export type OverlayWindowDebugStatus = {
  exists: boolean;
  visible: boolean | null;
  label: string;
  title: string | null;
  pos: { x: number; y: number } | null;
  size: { w: number; h: number } | null;
  error: string | null;
};

export async function getOverlayWindowDebugStatus(): Promise<OverlayWindowDebugStatus> {
  if (!isTauri()) {
    return {
      exists: false,
      visible: null,
      label: OVERLAY_WINDOW_LABEL,
      title: null,
      pos: null,
      size: null,
      error: "not running in tauri"
    };
  }
  try {
    const { WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const win = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (!win) {
      return { exists: false, visible: null, label: OVERLAY_WINDOW_LABEL, title: null, pos: null, size: null, error: null };
    }
    const [visible, title, pos, size] = await Promise.all([
      win.isVisible(),
      win.title(),
      win.outerPosition(),
      win.outerSize()
    ]);
    return {
      exists: true,
      visible,
      label: OVERLAY_WINDOW_LABEL,
      title,
      pos: { x: pos.x, y: pos.y },
      size: { w: size.width, h: size.height },
      error: null
    };
  } catch (e) {
    return {
      exists: false,
      visible: null,
      label: OVERLAY_WINDOW_LABEL,
      title: null,
      pos: null,
      size: null,
      error: String(e)
    };
  }
}

export async function resetOverlayWindowBounds(): Promise<void> {
  pushOverlayDiag("resetOverlayWindowBounds()");
  try {
    localStorage.removeItem("helltime:overlayWindowBounds");
  } catch {
    // ignore
  }
  if (!isTauri()) return;
  try {
    const { LogicalPosition, LogicalSize, WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const win = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (!win) {
      await ensureOverlayWindow();
      return;
    }
    await win.setSize(new LogicalSize(340, 160));
    await win.setPosition(new LogicalPosition(40, 40));
    await win.show();
    await win.setAlwaysOnTop(true);
  } catch (e) {
    pushOverlayDiag(`resetOverlayWindowBounds failed: ${String(e)}`);
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
  pushOverlayDiag("broadcastOverlayWindowSettings()");
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
    pushOverlayDiag(`broadcastOverlayWindowSettings failed: ${String(e)}`);
  }
}
