import React, { useEffect, useMemo, useState } from "react";
import { isTauri } from "@tauri-apps/api/core";
import { fetchSchedule } from "./lib/helltides";
import { loadSettings } from "./lib/settings";
import { formatCountdown, formatLocalTime } from "./lib/time";
import type { ScheduleResponse, ScheduleType, WorldBossScheduleItem } from "./lib/types";

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

export default function OverviewOverlay() {
  const [schedule, setSchedule] = useState<ScheduleResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [now, setNow] = useState(() => Date.now());
  const [lastRefreshAt, setLastRefreshAt] = useState<number | null>(null);
  const [settingsVersion, setSettingsVersion] = useState(0);
  const [settings, setSettings] = useState(() => loadSettings());

  useEffect(() => {
    const id = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(id);
  }, []);

  useEffect(() => {
    document.body.classList.add("overviewMode");
    return () => document.body.classList.remove("overviewMode");
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
    const onStorage = (e: StorageEvent) => {
      if (e.key === "settings_v6" || e.key === "settings_v7") setSettingsVersion((v) => v + 1);
    };
    window.addEventListener("storage", onStorage);
    return () => window.removeEventListener("storage", onStorage);
  }, []);

  useEffect(() => {
    const next = loadSettings();
    setSettings(next);
    if (!next.overviewOverlayEnabled && isTauri()) {
      void (async () => {
        try {
          const { WebviewWindow } = await import("@tauri-apps/api/webviewWindow");
          await WebviewWindow.getCurrent().hide();
        } catch {
          // ignore
        }
      })();
    }
  }, [settingsVersion]);

  const nextByType = useMemo(() => {
    if (!schedule) return null;
    return {
      helltide: findNext(schedule.helltide, now),
      legion: findNext(schedule.legion, now),
      world_boss: findNext(schedule.world_boss, now)
    };
  }, [schedule, now]);

  const ordered = useMemo(() => {
    if (!nextByType) return [...types];
    const enabledTypes = types.filter((t) => settings.overviewOverlayCategories?.[t] !== false);
    return [...enabledTypes]
      .map((type) => {
        const next = nextByType[type];
        const startMs = next ? new Date(next.startTime).getTime() : Number.POSITIVE_INFINITY;
        return { type, startMs };
      })
      .sort((a, b) => a.startMs - b.startMs)
      .map((x) => x.type);
  }, [nextByType, settings.overviewOverlayCategories]);

  const lastUpdateLabel = useMemo(() => {
    if (!lastRefreshAt) return "—";
    try {
      return new Date(lastRefreshAt).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
    } catch {
      return "—";
    }
  }, [lastRefreshAt]);

  return (
    <div className="container overlayOverview" data-tauri-drag-region>
      <div className="overlayOverviewHeader" data-tauri-drag-region>
        <div className="overlayOverviewBrand" data-tauri-drag-region>
          helltime
        </div>
        <div className="overlayOverviewMeta" data-tauri-drag-region>
          {lastUpdateLabel}
        </div>
      </div>

      {error ? <div className="overlayOverviewError">Fehler</div> : null}

      <div className="overlayOverviewRows">
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
