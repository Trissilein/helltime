import React, { useEffect, useMemo, useRef, useState } from "react";
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

function invertHexColor(hex: string): string {
  const m = /^#([0-9a-fA-F]{6})$/.exec((hex ?? "").trim());
  if (!m) return "#f4f5f7";
  const rgb = parseInt(m[1], 16);
  const r = 255 - ((rgb >> 16) & 0xff);
  const g = 255 - ((rgb >> 8) & 0xff);
  const b = 255 - (rgb & 0xff);
  const out = (r << 16) | (g << 8) | b;
  return `#${out.toString(16).padStart(6, "0")}`;
}

function readPositioningUntil(): number {
  try {
    const raw = localStorage.getItem("helltime:overlayPositioningUntil");
    const n = Number(raw);
    return Number.isFinite(n) ? n : 0;
  } catch {
    return 0;
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
  const hostRef = useRef<HTMLDivElement | null>(null);
  const requestResizeRef = useRef<(() => void) | null>(null);
  const scaleXRef = useRef(1);
  const positioningRef = useRef(false);
  const [schedule, setSchedule] = useState<ScheduleResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [now, setNow] = useState(() => Date.now());
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
            overlayScaleX:
              typeof p.scaleX === "number"
                ? p.scaleX
                : typeof p.scale === "number"
                  ? p.scale
                  : (s as any).overlayScaleX ?? 1,
            overlayScaleY:
              typeof p.scaleY === "number"
                ? p.scaleY
                : typeof p.scale === "number"
                  ? p.scale
                  : (s as any).overlayScaleY ?? 1
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

  const scaleX = clampFloat(settings.overlayScaleX, 1, 0.6, 2.0);
  const scaleY = clampFloat(settings.overlayScaleY, 1, 0.6, 2.0);
  const positioning = useMemo(() => now < readPositioningUntil(), [now]);
  useEffect(() => {
    positioningRef.current = positioning;
  }, [positioning]);
  const bgAlpha = positioning ? 1 : clampFloat(settings.overlayBgOpacity, 0.2, 0, 1.0);
  const bgHex = positioning ? invertHexColor(settings.overlayBgHex) : settings.overlayBgHex;
  const bg = hexToRgba(bgHex, bgAlpha);

  const toastVisible = useMemo(() => {
    if (!toast) return false;
    const ms = toast.payload.durationMs ?? 5200;
    return now - toast.shownAt < ms;
  }, [toast, now]);

  const mode = settings.overlayWindowMode === "toast" ? "toast" : "overview";

  useEffect(() => {
    scaleXRef.current = scaleX;
  }, [scaleX]);

  useEffect(() => {
    requestResizeRef.current?.();
  }, [mode, toastVisible, ordered.length, scaleX, scaleY, bgAlpha]);

  useEffect(() => {
    if (!isTauri()) return;
    void (async () => {
      try {
        const { getCurrentWebviewWindow } = await import("@tauri-apps/api/webviewWindow");
        const win = getCurrentWebviewWindow();
        // Safety: never block clicks unless we're in explicit positioning mode.
        await win.setIgnoreCursorEvents(!positioningRef.current);
      } catch {
        // ignore
      }
    })();
  }, [positioning]);

  useEffect(() => {
    if (!isTauri()) return;
    let ro: ResizeObserver | null = null;
    let lastW = 0;
    let lastH = 0;
    let timer: number | null = null;

    void (async () => {
      try {
        const { getCurrentWebviewWindow } = await import("@tauri-apps/api/webviewWindow");
        const { LogicalSize } = await import("@tauri-apps/api/dpi");
        const win = getCurrentWebviewWindow();

        const commit = () => {
          if (!hostRef.current) return;
          const currentScaleX = scaleXRef.current;
          const contentH = Math.ceil(hostRef.current.scrollHeight);

          // Fixed-ish width (worst case) so layout doesn't jump when categories change.
          const baseW = 220;
          const w = Math.max(140, Math.round(baseW * currentScaleX));

          // Dynamic height (from content) + small padding to avoid rounding-clips.
          const h = Math.max(40, contentH + 8);
          if (Math.abs(w - lastW) <= 1 && Math.abs(h - lastH) <= 1) return;
          lastW = w;
          lastH = h;
          void win.setSize(new LogicalSize(w, h));
        };

        const schedule = () => {
          if (timer) window.clearTimeout(timer);
          timer = window.setTimeout(() => {
            timer = null;
            commit();
          }, 50);
        };

        requestResizeRef.current = schedule;
        ro = new ResizeObserver(() => schedule());
        if (hostRef.current) ro.observe(hostRef.current);

        // Also commit once after initial render.
        schedule();
      } catch {
        // ignore
      }
    })();

    return () => {
      try {
        ro?.disconnect();
      } catch {
        // ignore
      }
      if (timer) window.clearTimeout(timer);
      requestResizeRef.current = null;
    };
  }, []);

  useEffect(() => {
    if (!isTauri()) return;
    void (async () => {
      try {
        const { getCurrentWebviewWindow } = await import("@tauri-apps/api/webviewWindow");
        const win = getCurrentWebviewWindow();
        await win.setAlwaysOnTop(true);
      } catch {
        // ignore
      }
    })();
  }, []);

  async function startDragging(e: React.PointerEvent): Promise<void> {
    if (!isTauri()) return;
    if (e.button !== 0) return;
    try {
      e.preventDefault();
      const { getCurrentWebviewWindow } = await import("@tauri-apps/api/webviewWindow");
      await getCurrentWebviewWindow().startDragging();
    } catch {
      try {
        const { getCurrentWindow } = await import("@tauri-apps/api/window");
        await getCurrentWindow().startDragging();
      } catch {
        // ignore
      }
    }
  }

  return (
    <div
      className={`overlayHost overlayMode-${mode} ${positioning ? "positioning" : ""}`}
      style={{
        background: mode === "toast" && !toastVisible && !positioning ? "rgba(0,0,0,0)" : bg,
        ["--overlayScale" as any]: String(scaleY)
      }}
      ref={hostRef}
    >
      {positioning ? (
        <div
          className="overlayDragHandle"
          data-tauri-drag-region
          onPointerDown={(e) => void startDragging(e)}
          role="button"
          aria-label="Overlay verschieben"
          title="Ziehen zum Verschieben"
        >
          <span className="overlayDragHandleText">Ziehen zum Verschieben</span>
        </div>
      ) : null}

      {error ? <div className="overlayError">Fehler</div> : null}

      {mode === "toast" ? (
        toast && toastVisible ? (
          <div className={`overlayToast ${toast.payload.type ?? ""}`} data-tauri-drag-region>
            <div className="overlayToastLine">
              <span className="overlayToastEvent">{toast.payload.title}</span>
              <span className="overlayToastTime">{toast.payload.body}</span>
            </div>
          </div>
        ) : positioning ? (
          <div className="overlayToast" data-tauri-drag-region>
            <div className="overlayToastLine">
              <span className="overlayToastEvent">Overlay</span>
              <span className="overlayToastTime">ziehen</span>
            </div>
          </div>
        ) : null
      ) : (
        <div className="overlayLines" data-tauri-drag-region>
          {ordered.length === 0 ? <div className="overlayEmpty">—</div> : null}
          {ordered.map((type) => {
            const next = nextByType ? nextByType[type] : null;
            const startMs = next ? new Date(next.startTime).getTime() : null;
            const remaining = startMs ? formatCountdown(startMs - now) : "—";
            const name = getEventName(type, next);
            const showSubline = type === "world_boss" && Boolean(name.subtitle);

            return (
              <div className={`overlayLine ${type}`} key={type} data-tauri-drag-region>
                <span className="overlayLineEvent">
                  <span className="overlayLineEventTitle">{name.title}</span>
                  {showSubline ? <span className="overlayLineEventSub">{name.subtitle}</span> : null}
                </span>
                <span className="overlayLineTime">{remaining}</span>
              </div>
            );
          })}
        </div>
      )}

      {/* keep the window draggable even in empty areas */}
    </div>
  );
}
