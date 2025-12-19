import { isTauri } from "@tauri-apps/api/core";

type WindowBounds = { x: number; y: number; w: number; h: number };

function readBounds(storageKey: string): WindowBounds | null {
  try {
    const raw = localStorage.getItem(storageKey);
    if (!raw) return null;
    const obj = JSON.parse(raw) as Partial<WindowBounds>;
    if (typeof obj.x !== "number" || typeof obj.y !== "number" || typeof obj.w !== "number" || typeof obj.h !== "number") return null;
    if (![obj.x, obj.y, obj.w, obj.h].every(Number.isFinite)) return null;
    return { x: Math.round(obj.x), y: Math.round(obj.y), w: Math.round(obj.w), h: Math.round(obj.h) };
  } catch {
    return null;
  }
}

function writeBounds(storageKey: string, bounds: WindowBounds): void {
  try {
    localStorage.setItem(storageKey, JSON.stringify(bounds));
  } catch {
    // ignore
  }
}

export async function initWindowPersistence(storageKey: string): Promise<void> {
  if (!isTauri()) return;
  try {
    const { LogicalPosition, LogicalSize, getCurrentWebviewWindow } = await import("@tauri-apps/api/webviewWindow");
    const win = getCurrentWebviewWindow();

    const saved = readBounds(storageKey);
    if (saved) {
      try {
        await win.setSize(new LogicalSize(saved.w, saved.h));
        await win.setPosition(new LogicalPosition(saved.x, saved.y));
      } catch {
        // ignore
      }
    }

    let timer: number | null = null;
    async function scheduleSave() {
      if (timer) window.clearTimeout(timer);
      timer = window.setTimeout(async () => {
        timer = null;
        try {
          const [pos, size] = await Promise.all([win.outerPosition(), win.outerSize()]);
          writeBounds(storageKey, { x: pos.x, y: pos.y, w: size.width, h: size.height });
        } catch {
          // ignore
        }
      }, 250);
    }

    const unlistenMoved = await win.onMoved(() => void scheduleSave());
    const unlistenResized = await win.onResized(() => void scheduleSave());

    window.addEventListener("beforeunload", () => {
      try {
        unlistenMoved();
        unlistenResized();
      } catch {
        // ignore
      }
    });
  } catch {
    // ignore
  }
}

export async function initMainWindowPersistence(): Promise<void> {
  return initWindowPersistence("helltime:mainWindowBounds");
}
