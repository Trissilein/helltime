import React, { useEffect, useMemo, useRef, useState } from "react";
import { isTauri } from "@tauri-apps/api/core";
import { fetchSchedule } from "./lib/helltides";
import { formatCountdown, formatLocalTime } from "./lib/time";
import { loadSettings, saveSettings, type BeepPattern, type Settings, type TimerSettings } from "./lib/settings";
import { playBeep } from "./lib/sound";
import { formatRemainingSpeech, speak } from "./lib/speech";
import type { ScheduleResponse, ScheduleType, WorldBossScheduleItem } from "./lib/types";
import { disablePanicStop, isPanicStopEnabled } from "./lib/safety";
import {
  broadcastOverlayWindowSettings,
  ensureOverlayWindow,
  getOverlayWindowDebugStatus,
  OVERLAY_WINDOW_LABEL,
  resetOverlayWindowBounds,
  setOverlayWindowInteractive,
  setOverlayWindowVisible
} from "./lib/overlay_window";
import { clearOverlayDiag, readOverlayDiag } from "./lib/overlay_diag";

type FiredMap = Record<string, number>;

const FIRED_KEY = "helltime:fired_v3";
const OLD_FIRED_KEY = "helltime:fired_v2";

function loadFired(): FiredMap {
  try {
    const raw = localStorage.getItem(FIRED_KEY) ?? localStorage.getItem(OLD_FIRED_KEY);
    if (!raw) return {};
    return JSON.parse(raw) as FiredMap;
  } catch {
    return {};
  }
}

function saveFired(map: FiredMap): void {
  localStorage.setItem(FIRED_KEY, JSON.stringify(map));
}

function pruneFired(map: FiredMap, now: number): FiredMap {
  const next: FiredMap = {};
  const keepAfter = now - 1000 * 60 * 60 * 12;
  for (const [k, v] of Object.entries(map)) {
    if (typeof v === "number" && v >= keepAfter) next[k] = v;
  }
  return next;
}

function typeLabel(type: ScheduleType): string {
  switch (type) {
    case "helltide":
      return "Helltide";
    case "legion":
      return "Legion";
    case "world_boss":
      return "World Boss";
  }
}

function spokenTypeLabel(type: ScheduleType): string {
  switch (type) {
    case "helltide":
      return "Höllenhochwasser";
    case "legion":
      return "Legionellen";
    case "world_boss":
      return "Weltscheff";
  }
}

function findNext<T extends { startTime: string }>(items: T[], now: number): T | null {
  let best: T | null = null;
  let bestStartMs = Number.POSITIVE_INFINITY;
  for (const item of items) {
    const startMs = new Date(item.startTime).getTime();
    if (!Number.isFinite(startMs)) continue;
    if (startMs <= now) continue;
    if (startMs < bestStartMs) {
      best = item;
      bestStartMs = startMs;
    }
  }
  return best;
}

function getEventName(type: ScheduleType, item: { startTime: string } | null): string {
  if (!item) return typeLabel(type);
  if (type === "world_boss") {
    const boss = (item as WorldBossScheduleItem).boss;
    return boss ? `World Boss ${boss}` : "World Boss";
  }
  return typeLabel(type);
}

function getEventTitleParts(type: ScheduleType, item: { startTime: string } | null): { title: string; subtitle?: string } {
  if (!item) return { title: typeLabel(type) };
  if (type === "world_boss") {
    const boss = (item as WorldBossScheduleItem).boss;
    return boss ? { title: "World Boss", subtitle: boss } : { title: "World Boss" };
  }
  return { title: typeLabel(type) };
}

function getSpokenEventNameWithTemplate(
  type: ScheduleType,
  item: { startTime: string } | null,
  template: string | null | undefined
): string {
  const base = (template ?? "").trim() || spokenTypeLabel(type);
  if (type !== "world_boss") return base;
  const boss = item ? (item as WorldBossScheduleItem).boss : null;
  if (!boss) return base.replaceAll("{boss}", "").replaceAll("  ", " ").trim();
  if (base.includes("{boss}")) return base.replaceAll("{boss}", boss).replaceAll("  ", " ").trim();
  return `${base} ${boss}`.trim();
}

function clampInt(n: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, Math.round(n)));
}

const OPACITY_GAMMA = 2.6;
function opacityToSlider(alpha: number): number {
  const a = Math.max(0, Math.min(1, alpha));
  return clampInt(Math.round(Math.pow(a, 1 / OPACITY_GAMMA) * 100), 0, 100);
}

function sliderToOpacity(slider: number): number {
  const t = Math.max(0, Math.min(100, slider)) / 100;
  return Math.max(0, Math.min(1, Math.pow(t, OPACITY_GAMMA)));
}

const types: ScheduleType[] = ["helltide", "legion", "world_boss"];

export default function App() {
  const [schedule, setSchedule] = useState<ScheduleResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [now, setNow] = useState(() => Date.now());
  const [lastRefreshAt, setLastRefreshAt] = useState<number | null>(null);
  const [nextAutoRefreshAt, setNextAutoRefreshAt] = useState<number | null>(null);
  const autoRefreshTimeoutRef = useRef<number | null>(null);
  const refreshInFlightRef = useRef(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [debugOpen, setDebugOpen] = useState(false);
  const [overlayDebug, setOverlayDebug] = useState<string | null>(null);
  const [openCategory, setOpenCategory] = useState<ScheduleType | null>(null);
  const [panicStopEnabled, setPanicStopEnabled] = useState(() => isPanicStopEnabled());

  const [settings, setSettings] = useState<Settings>(() => loadSettings());
  const firedRef = useRef<FiredMap>(loadFired());
  const lastSettingsRef = useRef<Settings>(settings);

  function updateSettings(updater: (prev: Settings) => Settings): void {
    setSettings((prev) => {
      const next = updater(prev);
      lastSettingsRef.current = next;
      saveSettings(next);
      queueMicrotask(() => void broadcastOverlayWindowSettings(next));
      return next;
    });
  }

  useEffect(() => {
    const id = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(id);
  }, []);

  const categoryLayoutKey = useMemo(() => {
    return types
      .map((t) => {
        const c = settings.categories[t];
        return `${t}:${c.enabled ? 1 : 0}:${c.timerCount}`;
      })
      .join("|");
  }, [settings.categories]);

  useEffect(() => {
    if (!isTauri()) return;
    if (panicStopEnabled) return;

    const timer = window.setTimeout(() => {
      void (async () => {
        try {
          const { getCurrentWindow } = await import("@tauri-apps/api/window");
          const { LogicalSize } = await import("@tauri-apps/api/dpi");
          const win = getCurrentWindow();
          const currentPhysical = await win.innerSize();
          const factor = await win.scaleFactor();
          const logical = currentPhysical.toLogical(factor);

          let desired: number;
          if (settingsOpen) {
            const backdrop = document.querySelector(".modalBackdrop") as HTMLElement | null;
            const header = document.querySelector(".modalHeader") as HTMLElement | null;
            const body = document.querySelector(".modalBody") as HTMLElement | null;

            const padTop = backdrop ? Math.ceil(Number.parseFloat(getComputedStyle(backdrop).paddingTop) || 0) : 0;
            const padBottom = backdrop ? Math.ceil(Number.parseFloat(getComputedStyle(backdrop).paddingBottom) || 0) : 0;
            const headerH = header ? Math.ceil(header.getBoundingClientRect().height) : 0;
            const bodyH = body ? Math.ceil(body.scrollHeight) : Math.ceil(document.documentElement.scrollHeight);

            desired = clampInt(padTop + headerH + bodyH + padBottom, 360, 980);
          } else {
            const container = document.querySelector(".container") as HTMLElement | null;
            const contentH = container ? Math.ceil(container.getBoundingClientRect().height) : Math.ceil(document.documentElement.scrollHeight);
            const floating = document.querySelector(".floatingOverlayControls") as HTMLElement | null;
            const floatingH = floating ? Math.ceil(floating.getBoundingClientRect().height) : 0;
            desired = clampInt(contentH + floatingH + 36, 360, 980);
          }

          if (Math.abs(desired - logical.height) <= 16) return;
          await win.setSize(new LogicalSize(logical.width, desired));
        } catch {
          // ignore
        }
      })();
    }, 120);

    return () => window.clearTimeout(timer);
  }, [openCategory, categoryLayoutKey, settingsOpen, debugOpen, overlayDebug, panicStopEnabled]);

  useEffect(() => {
    if (!settingsOpen) return;
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === "Escape") setSettingsOpen(false);
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [settingsOpen]);

  useEffect(() => {
    const onPanic = (e: Event) => {
      const enabled = Boolean((e as CustomEvent).detail?.enabled);
      setPanicStopEnabled(enabled);
    };
    window.addEventListener("helltime:panic-stop", onPanic);
    return () => window.removeEventListener("helltime:panic-stop", onPanic);
  }, []);

  useEffect(() => {
    lastSettingsRef.current = settings;
  }, [settings]);

  useEffect(() => {
    void broadcastOverlayWindowSettings(settings);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (panicStopEnabled) {
      void setOverlayWindowVisible(false);
      return;
    }
    if (!settings.overlayWindowEnabled) {
      void setOverlayWindowVisible(false);
      return;
    }

    void (async () => {
      await ensureOverlayWindow();
      if (settings.overlayWindowMode === "overview") {
        await setOverlayWindowVisible(true);
      } else {
        // toast mode: keep it hidden until a toast arrives
        await setOverlayWindowVisible(false);
      }
    })();
  }, [settings.overlayWindowEnabled, settings.overlayWindowMode, panicStopEnabled]);

  useEffect(() => {
    firedRef.current = pruneFired(firedRef.current, now);
    saveFired(firedRef.current);
  }, [now]);

  async function refresh() {
    if (refreshInFlightRef.current) return;
    refreshInFlightRef.current = true;
    setError(null);
    try {
      const data = await fetchSchedule();
      data.helltide.sort((a, b) => a.timestamp - b.timestamp);
      data.legion.sort((a, b) => a.timestamp - b.timestamp);
      data.world_boss.sort((a, b) => a.timestamp - b.timestamp);
      setSchedule(data);
      setLastRefreshAt(Date.now());
    } catch (e) {
      setError(String(e));
    } finally {
      refreshInFlightRef.current = false;
    }
  }

  function randomAutoRefreshMs(): number {
    const minMs = 10 * 60_000;
    const maxMs = 15 * 60_000;
    return Math.floor(minMs + Math.random() * (maxMs - minMs + 1));
  }

  function scheduleNextAutoRefresh(baseNow: number): void {
    const nextAt = baseNow + randomAutoRefreshMs();
    setNextAutoRefreshAt(nextAt);
    if (autoRefreshTimeoutRef.current) window.clearTimeout(autoRefreshTimeoutRef.current);
    autoRefreshTimeoutRef.current = window.setTimeout(() => {
      void refresh();
    }, Math.max(1000, nextAt - Date.now()));
  }

  useEffect(() => {
    void refresh();
  }, []);

  useEffect(() => {
    if (panicStopEnabled) {
      if (autoRefreshTimeoutRef.current) window.clearTimeout(autoRefreshTimeoutRef.current);
      autoRefreshTimeoutRef.current = null;
      setNextAutoRefreshAt(null);
      return;
    }

    scheduleNextAutoRefresh(Date.now());
    return () => {
      if (autoRefreshTimeoutRef.current) window.clearTimeout(autoRefreshTimeoutRef.current);
      autoRefreshTimeoutRef.current = null;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [panicStopEnabled]);

  useEffect(() => {
    if (panicStopEnabled) return;
    if (!lastRefreshAt) return;
    scheduleNextAutoRefresh(lastRefreshAt);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [lastRefreshAt, panicStopEnabled]);

  const nextByType = useMemo(() => {
    if (!schedule) return null;
    return {
      helltide: findNext(schedule.helltide, now),
      legion: findNext(schedule.legion, now),
      world_boss: findNext(schedule.world_boss, now)
    };
  }, [schedule, now]);

  const orderedTypes = useMemo<ScheduleType[]>(() => {
    const enabled = types.filter((t) => settings.categories[t].enabled);
    const disabled = types.filter((t) => !settings.categories[t].enabled);
    if (!nextByType) return [...enabled, ...disabled];

    const enabledSorted = [...enabled]
      .map((type) => {
        const next = nextByType[type];
        const startMs = next ? new Date(next.startTime).getTime() : Number.POSITIVE_INFINITY;
        return { type, startMs };
      })
      .sort((a, b) => a.startMs - b.startMs)
      .map((x) => x.type);

    // Disabled categories are always at the bottom in stable order.
    return [...enabledSorted, ...disabled];
  }, [nextByType, settings.categories]);

  const nextEnabledOverall = useMemo(() => {
    if (!nextByType) return null;
    const candidates: Array<{ type: ScheduleType; startMs: number; startTime: string; name: string }> = [];

    for (const type of types) {
      if (!settings.categories[type].enabled) continue;
      const next = nextByType[type];
      if (!next) continue;
      const startMs = new Date(next.startTime).getTime();
      candidates.push({ type, startMs, startTime: next.startTime, name: getEventName(type, next) });
    }

    candidates.sort((a, b) => a.startMs - b.startMs);
    return candidates[0] ?? null;
  }, [nextByType, settings]);

  useEffect(() => {
    if (!schedule) return;
    if (panicStopEnabled) return;

    const fireWindowMs = 30_000;
    const ttsPauseMs = 500;

    for (const type of types) {
      const category = settings.categories[type];
      if (!category.enabled) continue;

      const next = findNext(schedule[type] as Array<{ id: number; startTime: string }>, now);
      if (!next) continue;

      const startMs = new Date(next.startTime).getTime();
      const remainingMs = startMs - now;
      const title = getEventName(type, next);
      const spokenTitle = getSpokenEventNameWithTemplate(type, next, category.ttsName);
      const timeLabel = formatLocalTime(next.startTime);

      for (let i = 0; i < category.timerCount; i++) {
        const timer = category.timers[i];
        const triggerMs = startMs - timer.minutesBefore * 60_000;
        if (now < triggerMs || now > triggerMs + fireWindowMs) continue;

        const key = `${type}:${next.id}:${i}`;
        if (firedRef.current[key]) continue;

        firedRef.current[key] = now;
        saveFired(firedRef.current);

        const body = formatCountdown(Math.max(0, remainingMs));
        void showOverlayToast({ title, body, type, kind: "event" });

        if (!settings.soundEnabled) continue;

        const beepMs = playBeep(timer.beepPattern, timer.pitchHz, settings.volume);

        if (timer.ttsEnabled) {
          window.setTimeout(() => {
            void speak(`${spokenTitle} in ${formatRemainingSpeech(Math.max(0, remainingMs))}`, settings.volume);
          }, beepMs + ttsPauseMs);
        }
      }
    }
  }, [schedule, now, settings, panicStopEnabled]);

  function setCategoryEnabled(type: ScheduleType, enabled: boolean) {
    if (enabled) {
      const nowMs = Date.now();
      const next = nextByType ? nextByType[type] : null;
      if (schedule && next) {
        const startMs = new Date(next.startTime).getTime();
        const remainingMs = startMs - nowMs;
        if (Number.isFinite(remainingMs) && remainingMs > 0) {
          const category = settings.categories[type];
          const candidates = Array.from({ length: category.timerCount })
            .map((_, i) => ({ i, timer: category.timers[i] }))
            .filter(({ timer }) => timer.minutesBefore * 60_000 >= remainingMs);

          candidates.sort((a, b) => a.timer.minutesBefore - b.timer.minutesBefore);
          const chosen = candidates[0];

          if (chosen) {
            const key = `${type}:${(next as any).id ?? next.startTime}:${chosen.i}`;
            if (!firedRef.current[key]) {
              firedRef.current[key] = nowMs;
              saveFired(firedRef.current);

              const title = getEventName(type, next);
              const spokenTitle = getSpokenEventNameWithTemplate(type, next, category.ttsName);
              const timeLabel = formatLocalTime(next.startTime);
              const body = formatCountdown(Math.max(0, remainingMs));
              void showOverlayToast({ title, body, type, kind: "event" });

              if (settings.soundEnabled) {
                const beepMs = playBeep(chosen.timer.beepPattern, chosen.timer.pitchHz, settings.volume);
                if (chosen.timer.ttsEnabled) {
                  window.setTimeout(() => {
                    void speak(`${spokenTitle} in ${formatRemainingSpeech(Math.max(0, remainingMs))}`, settings.volume);
                  }, beepMs + 500);
                }
              }
            }
          }
        }
      }
    }

    if (!enabled && openCategory === type) setOpenCategory(null);
    updateSettings((s) => ({
      ...s,
      categories: {
        ...s.categories,
        [type]: { ...s.categories[type], enabled }
      }
    }));
  }

  function setTimerCount(type: ScheduleType, timerCount: 1 | 2 | 3) {
    updateSettings((s) => ({
      ...s,
      categories: {
        ...s.categories,
        [type]: { ...s.categories[type], timerCount }
      }
    }));
  }

  function updateTimer(type: ScheduleType, index: number, patch: Partial<TimerSettings>) {
    updateSettings((s) => {
      const category = s.categories[type];
      const timers: Settings["categories"][ScheduleType]["timers"] = [category.timers[0], category.timers[1], category.timers[2]];
      timers[index] = { ...timers[index], ...patch };

      return {
        ...s,
        categories: {
          ...s.categories,
          [type]: { ...category, timers }
        }
      };
    });
  }

  function formatClock(ms: number): string {
    try {
      return new Date(ms).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
    } catch {
      return "—";
    }
  }

  async function showOverlayToast(payload: { title: string; body: string; type?: ScheduleType; kind?: "event" | "debug" }) {
    if (panicStopEnabled) return;

    if (settings.overlayWindowEnabled) {
      try {
        await ensureOverlayWindow();
        if (settings.overlayWindowMode === "toast") {
          await setOverlayWindowVisible(true);
        }
        const { emitTo } = await import("@tauri-apps/api/event");
        const durationMs = payload.kind === "debug" ? 8000 : 5200;
        await emitTo(OVERLAY_WINDOW_LABEL, "helltime:toast", {
          title: payload.title,
          body: payload.body,
          type: payload.type,
          durationMs
        });
        if (settings.overlayWindowMode === "toast") {
          window.setTimeout(() => {
            void setOverlayWindowVisible(false);
          }, durationMs + 50);
        }
      } catch (e) {
        console.warn("emitTo overlay failed", e);
      }
    }
  }

  function testVolumeBeep(volumeOverride?: number): void {
    if (panicStopEnabled) return;
    if (!settings.soundEnabled) return;
    const sampleTimer = settings.categories.helltide.timers[0];
    playBeep(sampleTimer.beepPattern, sampleTimer.pitchHz, typeof volumeOverride === "number" ? volumeOverride : settings.volume);
  }

  function previewOverlayToast(): void {
    if (panicStopEnabled) return;
    if (!settings.overlayWindowEnabled) return;
    const title = "Overlay Vorschau";
    const body = "00:30";
    void showOverlayToast({ title, body, type: "helltide", kind: "debug" });
  }

  async function refreshOverlayDebug(): Promise<void> {
    const status = await getOverlayWindowDebugStatus();
    const recent = readOverlayDiag().slice(-8);
    const lines = [
      `exists=${status.exists} visible=${status.visible} title=${status.title ?? "—"}`,
      status.size ? `size=${status.size.w}x${status.size.h}` : "",
      status.error ? `error=${status.error}` : "",
      ...recent.map((e) => `${new Date(e.ts).toLocaleTimeString()} ${e.msg}`)
    ].filter(Boolean);
    setOverlayDebug(lines.join("\n"));
  }

  async function bringOverlayToFront(): Promise<void> {
    if (panicStopEnabled) return;
    try {
      const until = Date.now() + 15_000;
      try {
        localStorage.setItem("helltime:overlayPositioningUntil", String(until));
      } catch {
        // ignore
      }
      await setOverlayWindowVisible(true);
      await setOverlayWindowInteractive(true);
      window.setTimeout(() => {
        try {
          localStorage.setItem("helltime:overlayPositioningUntil", "0");
        } catch {
          // ignore
        }
        void setOverlayWindowInteractive(false);
      }, 15_000);
      setOverlayDebug("Overlay ist 15s anklickbar (Positionieren). Danach wird es wieder klick-durch.");
    } catch (e) {
      setOverlayDebug(`Overlay nach vorne fehlgeschlagen: ${String(e)}`);
    }
  }

  return (
    <div className="container">
      <div className="header">
        <div className="title">
          <h1 className="brand">
            <span className="brandMain">hell</span>
            <span className="brandAccent">time</span>
          </h1>
          <p>Event Timers</p>
        </div>
        <div className="actions">
          <div className="actionStack">
            <div className="actionRow">
              <button
                className="iconBtn"
                type="button"
                aria-label="Einstellungen"
                title="Einstellungen"
                onClick={() => setSettingsOpen(true)}
              >
                <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true">
                  <path
                    fill="currentColor"
                    d="M19.14 12.94c.04-.31.06-.63.06-.94s-.02-.63-.06-.94l2.03-1.58a.5.5 0 0 0 .12-.64l-1.92-3.32a.5.5 0 0 0-.6-.22l-2.39.96a7.06 7.06 0 0 0-1.63-.94l-.36-2.54A.5.5 0 0 0 13.9 1h-3.8a.5.5 0 0 0-.49.42l-.36 2.54c-.58.23-1.12.54-1.63.94l-2.39-.96a.5.5 0 0 0-.6.22L2.7 7.48a.5.5 0 0 0 .12.64l2.03 1.58c-.04.31-.06.63-.06.94s.02.63.06.94L2.82 14.5a.5.5 0 0 0-.12.64l1.92 3.32c.13.22.39.3.6.22l2.39-.96c.5.4 1.05.71 1.63.94l.36 2.54c.04.24.25.42.49.42h3.8c.24 0 .45-.18.49-.42l.36-2.54c.58-.23 1.12-.54 1.63-.94l2.39.96c.22.09.47 0 .6-.22l1.92-3.32a.5.5 0 0 0-.12-.64l-2.03-1.56ZM12 15.5A3.5 3.5 0 1 1 12 8a3.5 3.5 0 0 1 0 7.5Z"
                  />
                </svg>
              </button>
            </div>
            <div className="subNote">
              Letztes Update: {lastRefreshAt ? formatClock(lastRefreshAt) : "—"}
            </div>
          </div>
        </div>
      </div>

      {panicStopEnabled ? (
        <div className="warning" style={{ marginTop: 10 }}>
          <div>
            <div className="warningTitle">Sicherheits-Stopp aktiv</div>
            <div className="warningBody">Overlay/Ton/Auto sind pausiert (UI-Fehler erkannt).</div>
          </div>
          <div className="actions">
            <button
              className="btn"
              type="button"
              onClick={() => {
                disablePanicStop();
                window.location.reload();
              }}
            >
              Reset
            </button>
          </div>
        </div>
      ) : null}

      {error ? (
        <div className="errorBanner" style={{ marginTop: 10 }}>
          Fehler: {error}
        </div>
      ) : null}

      {settingsOpen ? (
        <div
          className="modalBackdrop"
          role="dialog"
          aria-modal="true"
          aria-label="Einstellungen"
          onMouseDown={() => setSettingsOpen(false)}
        >
          <div className="modal" onMouseDown={(e) => e.stopPropagation()}>
            <div className="modalHeader">
              <div className="modalTitle">Einstellungen</div>
              <button className="iconBtn" type="button" aria-label="Schließen" title="Schließen" onClick={() => setSettingsOpen(false)}>
                <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true">
                  <path
                    fill="currentColor"
                    d="M18.3 5.71a1 1 0 0 0-1.41 0L12 10.59 7.11 5.7A1 1 0 0 0 5.7 7.11L10.59 12l-4.9 4.89a1 1 0 1 0 1.41 1.42L12 13.41l4.89 4.9a1 1 0 0 0 1.42-1.41L13.41 12l4.9-4.89a1 1 0 0 0-.01-1.4Z"
                  />
                </svg>
              </button>
            </div>

            <div className="modalBody">
              <div className="form">
	                <div className="settingsBlock">
	                  <div className="sectionTitle">Overlay</div>
	
	                  <div className="inline">
	                    <div className="hint">Overlay</div>
	                    <div className="pill small">{settings.overlayWindowEnabled ? "an" : "aus"}</div>
	                  </div>
	                  <div className="hint">An/Aus und Position unten rechts im Hauptfenster.</div>
	
	                  <div className="field">
	                    <label className="hint">
	                      Inhalt <span className="pill small">{settings.overlayWindowMode}</span>
	                    </label>
                    <div className="toggleRow" style={{ marginBottom: 8 }}>
                      <label className="toggle">
                        <input
                          type="radio"
                          name="overlayMode"
                          disabled={panicStopEnabled || !settings.overlayWindowEnabled}
                          checked={settings.overlayWindowMode === "overview"}
                          onChange={() => updateSettings((s) => ({ ...s, overlayWindowMode: "overview" }))}
                        />
                        <span className="toggleLabel">Overview</span>
                      </label>
                      <label className="toggle">
                        <input
                          type="radio"
                          name="overlayMode"
                          disabled={panicStopEnabled || !settings.overlayWindowEnabled}
                          checked={settings.overlayWindowMode === "toast"}
                          onChange={() => updateSettings((s) => ({ ...s, overlayWindowMode: "toast" }))}
                        />
                        <span className="toggleLabel">Toast</span>
                      </label>
                    </div>
                    <div className="toggleRow">
                      <label className="toggle">
                        <input
                          type="checkbox"
                          disabled={panicStopEnabled || !settings.overlayWindowEnabled}
                          checked={settings.overlayWindowCategories.legion}
                          onChange={(e) =>
                            updateSettings((s) => ({
                              ...s,
                              overlayWindowCategories: { ...s.overlayWindowCategories, legion: e.target.checked }
                            }))
                          }
                        />
                        <span className="toggleLabel">Legion</span>
                      </label>
                      <label className="toggle">
                        <input
                          type="checkbox"
                          disabled={panicStopEnabled || !settings.overlayWindowEnabled}
                          checked={settings.overlayWindowCategories.helltide}
                          onChange={(e) =>
                            updateSettings((s) => ({
                              ...s,
                              overlayWindowCategories: { ...s.overlayWindowCategories, helltide: e.target.checked }
                            }))
                          }
                        />
                        <span className="toggleLabel">Helltide</span>
                      </label>
                      <label className="toggle">
                        <input
                          type="checkbox"
                          disabled={panicStopEnabled || !settings.overlayWindowEnabled}
                          checked={settings.overlayWindowCategories.world_boss}
                          onChange={(e) =>
                            updateSettings((s) => ({
                              ...s,
                              overlayWindowCategories: { ...s.overlayWindowCategories, world_boss: e.target.checked }
                            }))
                          }
                        />
                        <span className="toggleLabel">World Boss</span>
                      </label>
                    </div>
                  </div>

                  <div className="inline">
                    <div className="hint">Look</div>
                    <div className="actions">
                      <input
                        type="color"
                        value={settings.overlayBgHex}
                        onChange={(e) => updateSettings((s) => ({ ...s, overlayBgHex: e.target.value }))}
                        title="Overlay Hintergrund"
                      />
                      <div className="pill">{settings.overlayBgHex}</div>
                      <button
                        className="btn"
                        type="button"
                        disabled={panicStopEnabled || !settings.overlayWindowEnabled}
                        onClick={() => previewOverlayToast()}
                      >
                        Vorschau
                      </button>
                      <button
                        className="btn"
                        type="button"
                        disabled={panicStopEnabled}
                        onClick={() => void resetOverlayWindowBounds()}
                      >
                        Reset
                      </button>
                    </div>
	                  </div>
	
	                  <div className="field">
	                    <label>
	                      Breite <span className="pill">{Math.round(settings.overlayScaleX * 100)}%</span>
	                    </label>
	                    <input
	                      type="range"
	                      min={60}
	                      max={200}
	                      step={5}
	                      value={Math.round(settings.overlayScaleX * 100)}
	                      onChange={(e) =>
	                        updateSettings((s) => ({
	                          ...s,
	                          overlayScaleX: clampInt(Number(e.target.value), 60, 200) / 100
	                        }))
	                      }
	                    />
	                  </div>
	
	                  <div className="field">
	                    <label>
	                      Höhe <span className="pill">{Math.round(settings.overlayScaleY * 100)}%</span>
	                    </label>
	                    <input
	                      type="range"
	                      min={60}
	                      max={200}
	                      step={5}
	                      value={Math.round(settings.overlayScaleY * 100)}
	                      onChange={(e) =>
	                        updateSettings((s) => ({
	                          ...s,
	                          overlayScaleY: clampInt(Number(e.target.value), 60, 200) / 100
	                        }))
	                      }
	                    />
	                  </div>
	
	                  <div className="field">
	                    <label>
	                      Hintergrund-Transparenz <span className="pill">{Math.round(settings.overlayBgOpacity * 100)}%</span>
	                    </label>
	                    <input
	                      type="range"
	                      min={0}
	                      max={100}
	                      step={1}
	                      value={opacityToSlider(settings.overlayBgOpacity)}
	                      onChange={(e) =>
	                        updateSettings((s) => ({
	                          ...s,
	                          overlayBgOpacity: sliderToOpacity(clampInt(Number(e.target.value), 0, 100))
	                        }))
	                      }
	                    />
	                  </div>
	                </div>

                <div className="settingsBlock">
                  <div className="sectionTitle">Ton</div>

                  <div className="inline">
                    <div className="hint">Benachrichtigungs-Ton</div>
                    <label className="toggle">
                      <input
                        type="checkbox"
                        disabled={panicStopEnabled}
                        checked={settings.soundEnabled}
                        onChange={(e) => updateSettings((s) => ({ ...s, soundEnabled: e.target.checked }))}
                      />
                      <span className="toggleLabel">{settings.soundEnabled ? "an" : "aus"}</span>
                    </label>
                  </div>

                  <div className="field">
                    <label>
                      Lautstärke <span className="pill">{Math.round(settings.volume * 100)}%</span>
                    </label>
                    <input
                      type="range"
                      min={0}
                      max={100}
                      step={1}
                      disabled={!settings.soundEnabled}
                      value={Math.round(settings.volume * 100)}
                      onChange={(e) => updateSettings((s) => ({ ...s, volume: clampInt(Number(e.target.value), 0, 100) / 100 }))}
                      onPointerUp={(e) => testVolumeBeep(clampInt(Number(e.currentTarget.value), 0, 100) / 100)}
                      onKeyUp={(e) => {
                        if (e.key === "Enter" || e.key === " ") {
                          const target = e.currentTarget as HTMLInputElement;
                          testVolumeBeep(clampInt(Number(target.value), 0, 100) / 100);
                        }
                      }}
                    />
                  </div>
                </div>

                <div className="modalFooter">
                  <button className="btn small" type="button" onClick={() => setDebugOpen((v) => !v)} aria-expanded={debugOpen}>
                    Debug
                  </button>
                </div>

	                {debugOpen ? (
	                  <div className="settingsBlock">
	                    <div className="sectionTitle">Debug</div>
	                    <div className="actions">
	                      <button className="btn" type="button" disabled={panicStopEnabled} onClick={() => void refreshOverlayDebug()}>
	                        Overlay Status
	                      </button>
	                      <button
	                        className="btn"
	                        type="button"
	                        disabled={panicStopEnabled}
	                        onClick={() => {
	                          clearOverlayDiag();
	                          setOverlayDebug(null);
	                        }}
	                      >
	                        Logs leeren
	                      </button>
	                    </div>
	                    {overlayDebug ? <pre className="overlayDebugBox">{overlayDebug}</pre> : null}
	                  </div>
	                ) : null}
              </div>
            </div>
          </div>
        </div>
      ) : null}

      <div className="grid">
        {orderedTypes.map((type) => {
          const category = settings.categories[type];
          const next = nextByType ? nextByType[type] : null;
          const nextStartMs = next ? new Date(next.startTime).getTime() : null;
          const countdown = nextStartMs ? formatCountdown(nextStartMs - now) : "—";
          const timeLabel = next ? formatLocalTime(next.startTime) : "—";
          const name = getEventName(type, next);
          const titleParts = getEventTitleParts(type, next);
          const spokenName = getSpokenEventNameWithTemplate(type, next, category.ttsName);

          const isOpen = category.enabled && openCategory === type;

          return (
            <div
              className={`card span12 categoryCard ${type} categoryDetails ${isOpen ? "open" : ""} ${category.enabled ? "" : "disabled"}`}
              key={type}
              aria-disabled={!category.enabled}
            >
              <div className="panelHeaderRow categoryHeader">
                <button
                  className="panelHeaderBtn categoryExpandBtn"
                  type="button"
                  disabled={!category.enabled}
                  aria-expanded={isOpen}
                  onClick={() => {
                    if (!category.enabled) return;
                    setOpenCategory((prev) => (prev === type ? null : type));
                  }}
                >
                  <span className="panelHeaderTitle categoryTitle" title={name}>
                    <span className="categoryTitleMain">{titleParts.title}</span>
                    <span className={`categoryTitleSub ${titleParts.subtitle ? "" : "placeholder"}`}>{titleParts.subtitle ?? "—"}</span>
                  </span>
                  <span className="panelHeaderMeta">{countdown}</span>
                </button>
                <div className="panelHeaderRight">
                  <div className="pill">{timeLabel}</div>
                  <label className="toggle">
                    <input
                      type="checkbox"
                      checked={category.enabled}
                      onChange={(e) => setCategoryEnabled(type, e.target.checked)}
                    />
                    <span className="toggleLabel">Erinnern</span>
                  </label>
                </div>
              </div>

              <div className="form">
                {category.enabled && isOpen ? (
                  <>
                    <div className="inline" style={{ justifyContent: "space-between" }}>
                      <div className="hint">Timer Anzahl</div>
                      <select
                        className="select"
                        value={category.timerCount}
                        onChange={(e) => setTimerCount(type, clampInt(Number(e.target.value), 1, 3) as 1 | 2 | 3)}
                      >
                        <option value={1}>1</option>
                        <option value={2}>2</option>
                        <option value={3}>3</option>
                      </select>
                    </div>

                    <div className="field">
                      <label>
                        TTS Name <span className="pill small">{typeLabel(type)}</span>
                      </label>
                      <input
                        className="textInput"
                        type="text"
                        value={category.ttsName}
                        placeholder={spokenTypeLabel(type) + (type === "world_boss" ? " {boss}" : "")}
                        onChange={(e) => {
                          const value = e.target.value;
                          updateSettings((s) => ({
                            ...s,
                            categories: {
                              ...s.categories,
                              [type]: { ...s.categories[type], ttsName: value }
                            }
                          }));
                        }}
                      />
                      {type === "world_boss" ? (
                        <div className="hint">
                          Optional: Platzhalter <span className="pill small">{"{boss}"}</span>.
                        </div>
                      ) : null}
                    </div>

                    {Array.from({ length: category.timerCount }).map((_, i) => {
                      const timer = category.timers[i];
                      if (!timer) return null;

                      return (
                        <div className="level" key={i}>
                          <div className="levelHeader">
                            <div className="levelTitle">{`Timer ${i + 1}`}</div>
                            <button
                              className="btn"
                              type="button"
                              onClick={() => {
                                if (panicStopEnabled) return;
                                if (!settings.soundEnabled) return;
                                const beepMs = playBeep(timer.beepPattern, timer.pitchHz, settings.volume);
                                if (timer.ttsEnabled) {
                                  window.setTimeout(() => {
                                    void speak(`${spokenName} in ${formatRemainingSpeech(timer.minutesBefore * 60_000)}`, settings.volume);
                                  }, beepMs + 500);
                                }
                              }}
                            >
                              Test
                            </button>
                          </div>

                          <div className="timerBody">
                            <div className="timerRow">
                              <div className="field" style={{ flex: 1, minWidth: '120px' }}>
                                <label>
                                  Minuten vorher: <span className="pill">{timer.minutesBefore} min</span>
                                </label>
                                <input
                                  type="range"
                                  min={5}
                                  max={60}
                                  step={5}
                                  value={timer.minutesBefore}
                                  onChange={(e) => updateTimer(type, i, { minutesBefore: clampInt(Number(e.target.value), 5, 60) })}
                                />
                              </div>

                              <label className="toggle">
                                <input
                                  type="checkbox"
                                  checked={timer.ttsEnabled}
                                  onChange={(e) => updateTimer(type, i, { ttsEnabled: e.target.checked })}
                                />
                                <span className="toggleLabel">TTS</span>
                              </label>

                              <div className="field" style={{ margin: 0, minWidth: '80px' }}>
                                <label>Beep</label>
                                <select
                                  className="select"
                                  value={timer.beepPattern}
                                  onChange={(e) => updateTimer(type, i, { beepPattern: e.target.value as BeepPattern })}
                                >
                                  <option value="beep">Beep</option>
                                  <option value="double">Double</option>
                                  <option value="triple">Triple</option>
                                </select>
                              </div>
                            </div>

                            <div className="timerRow">
                              <div className="field" style={{ flex: 1, minWidth: '120px' }}>
                                <label className="pitchRow">
                                  Tonhöhe: <span className="pill">{timer.pitchHz} Hz</span>
                                </label>
                                <input
                                  type="range"
                                  min={200}
                                  max={2000}
                                  step={100}
                                  value={timer.pitchHz}
                                  onChange={(e) => updateTimer(type, i, { pitchHz: clampInt(Number(e.target.value), 200, 2000) })}
                                />
                              </div>

                              <button
                                className="iconBtn"
                                type="button"
                                aria-label="Ton testen"
                                title="Ton testen"
                                disabled={!settings.soundEnabled}
                                onClick={() => {
                                  if (panicStopEnabled) return;
                                  if (!settings.soundEnabled) return;
                                  playBeep(timer.beepPattern, timer.pitchHz, settings.volume);
                                }}
                              >
                                <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true">
                                  <path
                                    fill="currentColor"
                                    d="M3 10v4c0 .55.45 1 1 1h3l4 4V5L7 9H4c-.55 0-1 .45-1 1zm13.5 2c0-1.77-1.02-3.29-2.5-4.03v8.05c1.48-.73 2.5-2.25 2.5-4.02zM14 3.23v2.06c2.89.86 5 3.54 5 6.71s-2.11 5.85-5 6.71v2.06c4.01-.91 7-4.49 7-8.77s-2.99-7.86-7-8.77z"
                                  />
                                </svg>
                              </button>
                            </div>
                          </div>
                        </div>
                      );
                    })}
                  </>
                ) : null}
              </div>
            </div>
          );
        })}
      </div>

      <div className="floatingOverlayControls" aria-label="Overlay Controls">
        <label className="toggle">
          <input
            type="checkbox"
            disabled={panicStopEnabled}
            checked={settings.overlayWindowEnabled}
            onChange={(e) => updateSettings((s) => ({ ...s, overlayWindowEnabled: e.target.checked }))}
          />
          <span className="toggleLabel">Overlay</span>
        </label>
        <button
          className="btn"
          type="button"
          disabled={panicStopEnabled || !settings.overlayWindowEnabled}
          onClick={() => void bringOverlayToFront()}
        >
          Position
        </button>
      </div>
    </div>
  );
}
