import { isTauri } from "@tauri-apps/api/core";
import type { Settings } from "./settings";
import type { ScheduleType } from "./types";
import { pushOverlayDiag } from "./overlay_diag";

export const OVERLAY_WINDOW_LABEL = "overlay";
const OVERLAY_WINDOW_VERSION_KEY = "helltime:overlayWindowVersion";
const OVERLAY_WINDOW_VERSION = "10";

const ENSURE_INFLIGHT_KEY = "__helltimeEnsureOverlayWindowInFlight";

function getEnsureInFlight(): Promise<void> | null {
  return ((globalThis as any)[ENSURE_INFLIGHT_KEY] as Promise<void> | null) ?? null;
}

function setEnsureInFlight(promise: Promise<void> | null): void {
  (globalThis as any)[ENSURE_INFLIGHT_KEY] = promise;
}

export async function ensureOverlayWindow(): Promise<void> {
  if (!isTauri()) return;
  const existingInFlight = getEnsureInFlight();
  if (existingInFlight) return existingInFlight;
  pushOverlayDiag("ensureOverlayWindow()");
  const ensurePromise = (async () => {
    const { WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const existing = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (existing) {
      const version = (() => {
        try {
          return localStorage.getItem(OVERLAY_WINDOW_VERSION_KEY);
        } catch {
          return null;
        }
      })();

      if (version === OVERLAY_WINDOW_VERSION) {
        // Always enforce workspace behavior even when the window already exists.
        // This prevents "sticky across desktops" behavior after runtime updates or persisted OS state.
        try {
          await existing.setVisibleOnAllWorkspaces(false);
        } catch {
          // ignore
        }
        pushOverlayDiag("ensureOverlayWindow: already exists");
        return;
      }

      // Recreate once when options change (e.g. transparency).
      try {
        await existing.destroy();
      } catch {
        // ignore
      }
    }

    const win = new WebviewWindow(OVERLAY_WINDOW_LABEL, {
      title: "helltime overlay",
      url: "/?view=overlay",
      center: false,
      // Best-effort: keep the overlay on the current virtual desktop.
      // (On Windows this might be ignored depending on runtime support.)
      visibleOnAllWorkspaces: false,
      x: 40,
      y: 40,
      width: 260,
      height: 130,
      decorations: false,
      alwaysOnTop: true,
      skipTaskbar: true,
      shadow: false,
      resizable: false,
      focus: false,
      focusable: true,
      transparent: true,
      visible: false
    });

    const created = new Promise<void>((resolve, reject) => {
      win.once("tauri://created", () => resolve());
      win.once("tauri://error", (e) => reject(e));
    });

    try {
      await created;
      pushOverlayDiag("overlay window created");
    } catch (e) {
      const msg = String((e as any)?.payload ?? e);
      pushOverlayDiag(`overlay window error: ${msg}`);
      // If another ensure call created it first, treat as success.
      if (msg.includes("already exists")) return;
      const already = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
      if (already) return;
      throw e;
    }

    // Make it non-blocking by default: click-through overlay.
    try {
      await win.setIgnoreCursorEvents(true);
    } catch {
      // ignore
    }

    try {
      await win.setBackgroundColor([0, 0, 0, 0]);
      await win.setAlwaysOnTop(true);
    } catch {
      // ignore
    }

    // Best-effort: keep it confined to the current workspace.
    try {
      await win.setVisibleOnAllWorkspaces(false);
    } catch {
      // ignore
    }

    // Best-effort to keep it reachable even if persistence is borked.
    try {
      const { LogicalPosition, LogicalSize } = await import("@tauri-apps/api/dpi");
      await win.setSize(new LogicalSize(260, 130));
      await win.setPosition(new LogicalPosition(40, 40));
    } catch {
      // ignore
    }

    try {
      localStorage.setItem(OVERLAY_WINDOW_VERSION_KEY, OVERLAY_WINDOW_VERSION);
    } catch {
      // ignore
    }
  })()
    .catch((e) => {
      // eslint-disable-next-line no-console
      console.warn("ensureOverlayWindow failed", e);
      pushOverlayDiag(`ensureOverlayWindow failed: ${String(e)}`);
    })
    .finally(() => {
      setEnsureInFlight(null);
    });

  setEnsureInFlight(ensurePromise);
  return ensurePromise;
}

export async function setOverlayWindowVisible(visible: boolean): Promise<void> {
  if (!isTauri()) return;
  pushOverlayDiag(`setOverlayWindowVisible(${visible})`);
  try {
    const { WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const win = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (!win) {
      pushOverlayDiag("setOverlayWindowVisible: no window");
      return;
    }
    if (visible) {
      await win.show();
      await win.setAlwaysOnTop(true);
      try {
        await win.setVisibleOnAllWorkspaces(false);
      } catch {
        // ignore
      }
    } else {
      await win.hide();
    }
  } catch (e) {
    // eslint-disable-next-line no-console
    console.warn("setOverlayWindowVisible failed", e);
    pushOverlayDiag(`setOverlayWindowVisible failed: ${String(e)}`);
  }
}

export async function setOverlayWindowInteractive(interactive: boolean): Promise<void> {
  if (!isTauri()) return;
  pushOverlayDiag(`setOverlayWindowInteractive(${interactive})`);
  try {
    const { WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    let win = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (!win && interactive) {
      await ensureOverlayWindow();
      win = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    }
    if (!win) return;
    // interactive=true => accept clicks; interactive=false => click-through
    await win.setIgnoreCursorEvents(!interactive);
    if (interactive) {
      await win.show();
      await win.setAlwaysOnTop(true);
      try {
        await win.setVisibleOnAllWorkspaces(false);
      } catch {
        // ignore
      }
      try {
        await win.setFocus();
      } catch {
        // ignore
      }
    }
  } catch (e) {
    pushOverlayDiag(`setOverlayWindowInteractive failed: ${String(e)}`);
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
    const { WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const existing = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (existing) {
      try {
        await existing.destroy();
      } catch {
        // ignore
      }
    }
    await ensureOverlayWindow();
    const win = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (!win) return;
    const { LogicalPosition, LogicalSize } = await import("@tauri-apps/api/dpi");
    await win.setSize(new LogicalSize(260, 130));
    await win.setPosition(new LogicalPosition(40, 40));
    await win.show();
    await win.setAlwaysOnTop(true);
  } catch (e) {
    pushOverlayDiag(`resetOverlayWindowBounds failed: ${String(e)}`);
  }
}

export async function destroyOverlayWindow(): Promise<void> {
  if (!isTauri()) return;
  try {
    const { WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const win = await WebviewWindow.getByLabel(OVERLAY_WINDOW_LABEL);
    if (!win) return;
    await win.destroy();
  } catch (e) {
    pushOverlayDiag(`destroyOverlayWindow failed: ${String(e)}`);
  }
}

export type OverlayWindowSettings = {
  enabled: boolean;
  mode: "overview" | "toast";
  categories: Record<ScheduleType, boolean>;
  bgHex: string;
  bgOpacity: number;
  scaleX: number;
  scaleY: number;
};

export function toOverlayWindowSettings(settings: Settings): OverlayWindowSettings {
  return {
    enabled: settings.overlayWindowEnabled,
    mode: settings.overlayWindowMode,
    categories: settings.overlayWindowCategories,
    bgHex: settings.overlayBgHex,
    bgOpacity: settings.overlayBgOpacity,
    scaleX: settings.overlayScaleX,
    scaleY: settings.overlayScaleY
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
