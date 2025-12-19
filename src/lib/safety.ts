import { overlayHide } from "./overlay";

const PANIC_KEY = "helltime:panicStop";
const PANIC_EVENT = "helltime:panic-stop";
let watchdogTimer: number | null = null;
let watchdogBadCount = 0;
let watchdogArmedAt = 0;

export function isPanicStopEnabled(): boolean {
  try {
    return localStorage.getItem(PANIC_KEY) === "1";
  } catch {
    return false;
  }
}

export async function enablePanicStop(reason?: unknown): Promise<void> {
  try {
    localStorage.setItem(PANIC_KEY, "1");
  } catch {
    // ignore
  }

  if (watchdogTimer) {
    window.clearInterval(watchdogTimer);
    watchdogTimer = null;
  }

  try {
    window.dispatchEvent(new CustomEvent(PANIC_EVENT, { detail: { enabled: true } }));
  } catch {
    // ignore
  }

  try {
    if ("speechSynthesis" in window) window.speechSynthesis.cancel();
  } catch {
    // ignore
  }

  try {
    await overlayHide();
  } catch {
    // ignore
  }

  // eslint-disable-next-line no-console
  console.error("helltime: panic stop enabled", reason);
}

export function disablePanicStop(): void {
  try {
    localStorage.removeItem(PANIC_KEY);
  } catch {
    // ignore
  }

  try {
    window.dispatchEvent(new CustomEvent(PANIC_EVENT, { detail: { enabled: false } }));
  } catch {
    // ignore
  }
}

export function startUiWatchdog(): void {
  if (watchdogTimer) return;
  watchdogBadCount = 0;
  watchdogArmedAt = Date.now() + 5000;

  watchdogTimer = window.setInterval(() => {
    if (isPanicStopEnabled()) return;
    if (Date.now() < watchdogArmedAt) return;

    const root = document.getElementById("root");
    const container = root?.querySelector(".container") as HTMLElement | null;
    const rect = container?.getBoundingClientRect();
    const ok = !!container && !!rect && rect.width > 200 && rect.height > 120;

    if (ok) {
      watchdogBadCount = 0;
      return;
    }

    watchdogBadCount++;
    if (watchdogBadCount >= 3) {
      void enablePanicStop(new Error("UI watchdog tripped"));
    }
  }, 1500);
}
