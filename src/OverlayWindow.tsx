import React, { useEffect, useMemo, useState } from "react";
import { isTauri } from "@tauri-apps/api/core";
import { fetchSchedule } from "./lib/helltides";
import { loadSettings } from "./lib/settings";
import { formatCountdown, formatLocalTime } from "./lib/time";
import type { ScheduleResponse, ScheduleType, WorldBossScheduleItem } from "./lib/types";

function clampFloat(n: unknown, fallback: number, min: number, max: number): number {
  if (typeof n !== "number" || !Number.isFinite(n)) return fallback;
  return Math.max(min, Math.min(max, n));
}

function hexToRgba(hex: string, alpha: number): string {
  const m = /^#([0-9a-fA-F]{6})$/.exec((hex ?? "").trim());
  if (!m) return `rgba(11,18,32,${alpha})`;
  const rgb = parseInt(m[1], 16);
  const r = (rgb >> 16) & 0xff;
  const g = (rgb >> 8) & 0xff;
  const b = rgb & 0xff;
  return `rgba(${r},${g},${b},${alpha})`;
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

function getEventName(type: ScheduleType, item: { startTime: string } | null): { title: string; subtitle?: string } {
  if (!item) return { title: typeLabel(type) };
  if (type === "world_boss") {
    const boss = (item as WorldBossScheduleItem).boss;
    return boss ? { title: "World Boss", subtitle: boss } : { title: "World Boss" };
  }
  return { title: typeLabel(type) };
}

const types: ScheduleType[] = ["helltide", "legion", "world_boss"];

type ToastPayload = {
  title: string;
  body: string;
  type?: ScheduleType;
  durationMs?: number;
};

export default function OverlayWindow() {
  const [schedule, setSchedule] = useState<ScheduleResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [now, setNow] = useState(() => Date.now());
  const [lastRefreshAt, setLastRefreshAt] = useState<number | null>(null);
  const [settings, setSettings] = useState(() => loadSettings());
  const [toast, setToast] = useState<{ payload: ToastPayload; shownAt: number } | null>(null);

  useEffect(() => {
    document.body.classList.add("overviewMode");
    return () => document.body.classList.remove("overviewMode");
  }, []);

  useEffect(() => {
    const id = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(id);
  }, []);

  useEffect(() => {
    setSettings(loadSettings());
  }, []);

  useEffect(() => {
    async function refresh() {
      try {
        const data = await fetchSchedule();
        data.helltide.sort((a, b) => a.timestamp - b.timestamp);
        data.legion.sort((a, b) => a.timestamp - b.timestamp);
        data.world_boss.sort((a, b) => a.timestamp - b.timestamp);
        setSchedule(data);
        setLastRefreshAt(Date.now());
        setError(null);
      } catch (e) {
        setError(String(e));
      }
    }

    void refresh();
    const id = window.setInterval(() => void refresh(), 60_000);
    return () => window.clearInterval(id);
  }, []);

  useEffect(() => {
    if (!isTauri()) return;
    let unlisten: (() => void) | null = null;
    void (async () => {
      try {
        const { listen } = await import("@tauri-apps/api/event");
        unlisten = await listen<ToastPayload>("helltime:toast", (event) => {
          const payload = event.payload;
          if (!payload?.title) return;
          setToast({ payload, shownAt: Date.now() });
        });
      } catch (e) {
        // eslint-disable-next-line no-console
        console.warn("overlay: listen helltime:toast failed", e);
      }
    })();

    return () => {
      try {
        unlisten?.();
      } catch {
        // ignore
      }
    };
  }, []);

  useEffect(() => {
    if (!isTauri()) return;
    let unlisten: (() => void) | null = null;
    void (async () => {
      try {
        const { listen } = await import("@tauri-apps/api/event");
        unlisten = await listen<any>("helltime:overlay-settings", (event) => {
          const p = event.payload as any;
          if (!p || typeof p !== "object") return;
          setSettings((s) => ({
            ...s,
            overlayWindowEnabled: typeof p.enabled === "boolean" ? p.enabled : s.overlayWindowEnabled,
            overlayWindowMode: p.mode === "toast" ? "toast" : "overview",
            overlayWindowCategories: typeof p.categories === "object" && p.categories ? p.categories : s.overlayWindowCategories,
            overlayBgHex: typeof p.bgHex === "string" ? p.bgHex : s.overlayBgHex,
            overlayBgOpacity: typeof p.bgOpacity === "number" ? p.bgOpacity : s.overlayBgOpacity,
            overlayScale: typeof p.scale === "number" ? p.scale : s.overlayScale
          }));
        });
      } catch (e) {
        // eslint-disable-next-line no-console
        console.warn("overlay: listen helltime:overlay-settings failed", e);
      }
    })();

    return () => {
      try {
        unlisten?.();
      } catch {
        // ignore
      }
    };
  }, []);

  const nextByType = useMemo(() => {
    if (!schedule) return null;
    return {
      helltide: findNext(schedule.helltide, now),
      legion: findNext(schedule.legion, now),
      world_boss: findNext(schedule.world_boss, now)
    };
  }, [schedule, now]);

  const enabledTypes = useMemo(() => {
    const cats = settings.overlayWindowCategories ?? { helltide: true, legion: true, world_boss: true };
    return types.filter((t) => cats[t] !== false);
  }, [settings.overlayWindowCategories]);

  const ordered = useMemo(() => {
    if (!nextByType) return [...enabledTypes];
    return [...enabledTypes]
      .map((type) => {
        const next = nextByType[type];
        const startMs = next ? new Date(next.startTime).getTime() : Number.POSITIVE_INFINITY;
        return { type, startMs };
      })
      .sort((a, b) => a.startMs - b.startMs)
      .map((x) => x.type);
  }, [enabledTypes, nextByType]);

  const lastUpdateLabel = useMemo(() => {
    if (!lastRefreshAt) return "—";
    try {
      return new Date(lastRefreshAt).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
    } catch {
      return "—";
    }
  }, [lastRefreshAt]);

  const scale = clampFloat(settings.overlayScale, 1, 0.6, 2.0);
  const bgAlpha = clampFloat(settings.overlayBgOpacity, 0.92, 0.2, 1.0);
  const bg = hexToRgba(settings.overlayBgHex, bgAlpha);

  const toastVisible = useMemo(() => {
    if (!toast) return false;
    const ms = toast.payload.durationMs ?? 5200;
    return now - toast.shownAt < ms;
  }, [toast, now]);

  useEffect(() => {
    if (!isTauri()) return;
    void (async () => {
      try {
        const { getCurrentWebviewWindow } = await import("@tauri-apps/api/webviewWindow");
        const win = getCurrentWebviewWindow();
        if (settings.overlayWindowMode === "toast") {
          if (toastVisible) {
            await win.show();
            await win.setAlwaysOnTop(true);
          } else {
            await win.hide();
          }
        } else {
          await win.show();
          await win.setAlwaysOnTop(true);
        }
      } catch {
        // ignore
      }
    })();
  }, [toastVisible, settings.overlayWindowMode]);

  return (
    <div
      className="container overlayOverview overlayHost"
      data-tauri-drag-region
      style={{ background: bg, transform: `scale(${scale})`, transformOrigin: "top left" }}
    >
      <div className="overlayOverviewHeader" data-tauri-drag-region>
        <div className="overlayOverviewBrand" data-tauri-drag-region>
          helltime
        </div>
        <div className="overlayOverviewMeta" data-tauri-drag-region>
          {settings.overlayWindowMode === "toast" ? "toast" : "overview"} • {lastUpdateLabel}
        </div>
      </div>

      {error ? <div className="overlayOverviewError">Fehler</div> : null}

      {toast && toastVisible ? (
        <div className={`overlayToast ${toast.payload.type ?? ""}`}>
          <div className="overlayToastTitle">{toast.payload.title}</div>
          <div className="overlayToastBody">{toast.payload.body}</div>
        </div>
      ) : null}

      <div className="overlayOverviewRows">
        {settings.overlayWindowMode === "toast" && !toastVisible ? <div className="overlayOverviewMeta">Warte auf Toast…</div> : null}
        {ordered.length === 0 ? <div className="overlayOverviewError">Keine Kategorien ausgewählt</div> : null}
        {ordered.map((type) => {
          const next = nextByType ? nextByType[type] : null;
          const startMs = next ? new Date(next.startTime).getTime() : null;
          const remaining = startMs ? formatCountdown(startMs - now) : "—";
          const timeLabel = next ? formatLocalTime(next.startTime) : "—";
          const name = getEventName(type, next);

          return (
            <div className={`overlayOverviewRow ${type}`} key={type}>
              <div className="overlayOverviewTitle">
                <div className="overlayOverviewTitleMain">{name.title}</div>
                {name.subtitle ? <div className="overlayOverviewTitleSub">{name.subtitle}</div> : null}
              </div>
              <div className="overlayOverviewRight">
                <div className="overlayOverviewCountdown">{remaining}</div>
                <div className="overlayOverviewTime">{timeLabel}</div>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
