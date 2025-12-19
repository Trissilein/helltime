import React, { useEffect, useMemo, useRef, useState } from "react";
import { fetchSchedule } from "./lib/helltides";
import { notify } from "./lib/notify";
import { formatCountdown, formatLocalTime } from "./lib/time";
import { loadSettings, saveSettings, type BeepPattern, type Settings, type TimerSettings } from "./lib/settings";
import { playBeep } from "./lib/sound";
import { formatRemainingSpeech, speak } from "./lib/speech";
import type { ScheduleResponse, ScheduleType, WorldBossScheduleItem } from "./lib/types";
import { overlayEnterConfig, overlayExitConfig, overlayGetPosition, overlayShow, overlayStatus } from "./lib/overlay";

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

function getSpokenEventName(type: ScheduleType, item: { startTime: string } | null): string {
  if (!item) return spokenTypeLabel(type);
  if (type === "world_boss") {
    const boss = (item as WorldBossScheduleItem).boss;
    return boss ? `${spokenTypeLabel(type)} ${boss}` : spokenTypeLabel(type);
  }
  return spokenTypeLabel(type);
}

function clampInt(n: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, Math.round(n)));
}

const types: ScheduleType[] = ["helltide", "legion", "world_boss"];

export default function App() {
  const [schedule, setSchedule] = useState<ScheduleResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [now, setNow] = useState(() => Date.now());
  const [lastRefreshAt, setLastRefreshAt] = useState<number | null>(null);
  const [overlayInfo, setOverlayInfo] = useState<Awaited<ReturnType<typeof overlayStatus>>>(null);

  const [settings, setSettings] = useState<Settings>(() => loadSettings());
  const firedRef = useRef<FiredMap>(loadFired());

  useEffect(() => {
    const id = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(id);
  }, []);

  useEffect(() => {
    saveSettings(settings);
  }, [settings]);

  useEffect(() => {
    firedRef.current = pruneFired(firedRef.current, now);
    saveFired(firedRef.current);
  }, [now]);

  async function refresh() {
    setLoading(true);
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
      setLoading(false);
    }
  }

  useEffect(() => {
    void refresh();
  }, []);

  const nextByType = useMemo(() => {
    if (!schedule) return null;
    return {
      helltide: findNext(schedule.helltide, now),
      legion: findNext(schedule.legion, now),
      world_boss: findNext(schedule.world_boss, now)
    };
  }, [schedule, now]);

  const orderedTypes = useMemo<ScheduleType[]>(() => {
    if (!nextByType) return [...types];
    return [...types]
      .map((type) => {
        const enabled = settings.categories[type].enabled;
        const next = nextByType[type];
        const startMs = next ? new Date(next.startTime).getTime() : Number.POSITIVE_INFINITY;
        return { type, enabled, startMs };
      })
      .sort((a, b) => {
        if (a.enabled !== b.enabled) return a.enabled ? -1 : 1;
        return a.startMs - b.startMs;
      })
      .map((x) => x.type);
  }, [nextByType, settings]);

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
      const spokenTitle = getSpokenEventName(type, next);
      const timeLabel = formatLocalTime(next.startTime);

      for (let i = 0; i < category.timerCount; i++) {
        const timer = category.timers[i];
        const triggerMs = startMs - timer.minutesBefore * 60_000;
        if (now < triggerMs || now > triggerMs + fireWindowMs) continue;

        const key = `${type}:${next.id}:${i}`;
        if (firedRef.current[key]) continue;

        firedRef.current[key] = now;
        saveFired(firedRef.current);

        const body = `Start in ${formatCountdown(Math.max(0, remainingMs))} • ${timeLabel}`;
        void showOverlayToast({ title, body, type, kind: "event" });
        if (settings.systemToastsEnabled) void notify(title, body);

        const beepMs = playBeep(timer.beepPattern, timer.pitchHz, settings.volume);

        if (timer.ttsEnabled) {
          window.setTimeout(() => {
            void speak(`${spokenTitle} in ${formatRemainingSpeech(Math.max(0, remainingMs))}`, settings.volume);
          }, beepMs + ttsPauseMs);
        }
      }
    }
  }, [schedule, now, settings]);

  function setCategoryEnabled(type: ScheduleType, enabled: boolean) {
    setSettings((s) => ({
      ...s,
      categories: {
        ...s.categories,
        [type]: { ...s.categories[type], enabled }
      }
    }));
  }

  function setTimerCount(type: ScheduleType, timerCount: 1 | 2 | 3) {
    setSettings((s) => ({
      ...s,
      categories: {
        ...s.categories,
        [type]: { ...s.categories[type], timerCount }
      }
    }));
  }

  function updateTimer(type: ScheduleType, index: number, patch: Partial<TimerSettings>) {
    setSettings((s) => {
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

  async function testSystemToast(): Promise<void> {
    await notify("helltime – Test", `System-Toast Test • ${formatClock(Date.now())}`);
  }

  async function showOverlayToast(payload: { title: string; body: string; type?: ScheduleType; kind?: "event" | "debug" }) {
    if (!settings.overlayToastsEnabled) return;
    await overlayShow(
      { title: payload.title, body: payload.body, kind: payload.kind, type: payload.type },
      settings.overlayToastsPosition
    );
  }

  async function startOverlayPositionMode(): Promise<void> {
    await overlayEnterConfig(settings.overlayToastsPosition);
  }

  async function saveOverlayPosition(): Promise<void> {
    const pos = await overlayGetPosition();
    if (!pos) return;
    setSettings((s) => ({ ...s, overlayToastsEnabled: true, overlayToastsPosition: pos }));
    await overlayExitConfig();
  }

  function testVolumeBeep(volumeOverride?: number): void {
    const sampleTimer = settings.categories.helltide.timers[0];
    playBeep(sampleTimer.beepPattern, sampleTimer.pitchHz, typeof volumeOverride === "number" ? volumeOverride : settings.volume);
  }

  async function fireAllDebug(): Promise<void> {
    const delay = (ms: number) => new Promise<void>((r) => window.setTimeout(r, ms));
    const ttsPauseMs = 500;

    for (const type of types) {
      const category = settings.categories[type];
      if (!category.enabled) continue;

      const title = typeLabel(type);
      for (let i = 0; i < category.timerCount; i++) {
        const timer = category.timers[i];
        const label = `Debug Timer ${i + 1} (${timer.minutesBefore} min)`;

        await showOverlayToast({ title, body: label, type, kind: "debug" });
        if (settings.systemToastsEnabled) {
          await notify(title, label);
          await delay(300);
        }

        const beepMs = playBeep(timer.beepPattern, timer.pitchHz, settings.volume);
        if (timer.ttsEnabled) {
          window.setTimeout(() => {
            void speak(`${title}. ${label}`, settings.volume);
          }, beepMs + ttsPauseMs);
        }

        await delay(Math.max(650, beepMs + 200));
      }
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
          <p>Helltide • Legion • World Boss</p>
        </div>
        <div className="actions">
          <div className="actionStack">
            <div className="actionRow">
              <button className="btn" onClick={() => void refresh()} disabled={loading}>
                {loading ? "Lade…" : "Refresh"}
              </button>
              <a className="btn primary" href="https://helltides.com" target="_blank" rel="noreferrer">
                Quelle
              </a>
            </div>
            <div className="subNote">Letztes Update: {lastRefreshAt ? formatClock(lastRefreshAt) : "—"}</div>
          </div>
        </div>
      </div>

      <div className="grid">
        <div className="card span12">
          <h2>Intro & Settings</h2>
          <div className="form">
            <div className="inline">
              <div className="hint">Nächstes Event (aktivierte Kategorien)</div>
              <div className="pill">
                {nextEnabledOverall ? `${nextEnabledOverall.name} • ${formatLocalTime(nextEnabledOverall.startTime)}` : "—"}
              </div>
            </div>
            <div className="inline">
              <div className="hint">System Toaster</div>
              <label className="toggle">
                <input
                  type="checkbox"
                  checked={settings.systemToastsEnabled}
                  onChange={(e) => setSettings((s) => ({ ...s, systemToastsEnabled: e.target.checked }))}
                />
                <span className="toggleLabel">anzeigen</span>
              </label>
            </div>
            <div className="inline">
              <div className="hint">Overlay Toast (Mini-Fenster)</div>
              <div className="actions">
                <label className="toggle">
                  <input
                    type="checkbox"
                    checked={settings.overlayToastsEnabled}
                    onChange={(e) => setSettings((s) => ({ ...s, overlayToastsEnabled: e.target.checked }))}
                  />
                  <span className="toggleLabel">aktiv</span>
                </label>
                <button className="btn" type="button" onClick={() => void startOverlayPositionMode()}>
                  Position
                </button>
                <button
                  className="btn"
                  type="button"
                  onClick={() => void saveOverlayPosition()}
                >
                  Speichern
                </button>
                <button className="btn" type="button" onClick={() => void showOverlayToast({ title: "helltime", body: "Overlay Toast Test", kind: "debug" })}>
                  Test
                </button>
                <button className="btn" type="button" onClick={() => void overlayStatus().then((s) => setOverlayInfo(s))}>
                  Status
                </button>
              </div>
            </div>
            {overlayInfo ? (
              <div className="hint">
                Overlay: {overlayInfo.supported ? "supported" : "n/a"} • running: {String(overlayInfo.running)} • visible:{" "}
                {String(overlayInfo.visible)} • config: {String(overlayInfo.config_mode)} • err: {overlayInfo.last_error ?? "—"}
              </div>
            ) : null}
            {settings.systemToastsEnabled ? (
              <div className="hint">
                Hinweis: Position/Anzeige hängt vom Betriebssystem ab (Windows: Fokusassist & Benachrichtigungseinstellungen prüfen).
              </div>
            ) : null}
            <div className="field">
              <label>
                Lautstärke: <span className="pill">{Math.round(settings.volume * 100)}%</span>
              </label>
              <input
                type="range"
                min={0}
                max={100}
                step={1}
                value={Math.round(settings.volume * 100)}
                onChange={(e) => setSettings((s) => ({ ...s, volume: clampInt(Number(e.target.value), 0, 100) / 100 }))}
                onPointerUp={(e) => testVolumeBeep(clampInt(Number(e.currentTarget.value), 0, 100) / 100)}
                onKeyUp={(e) => {
                  if (e.key === "Enter" || e.key === " ") {
                    const target = e.currentTarget as HTMLInputElement;
                    testVolumeBeep(clampInt(Number(target.value), 0, 100) / 100);
                  }
                }}
              />
            </div>
            <div className="inline">
              <div className="hint">Debug</div>
              <div className="actions">
                <button className="btn" type="button" onClick={() => void testSystemToast()}>
                  Test Toast
                </button>
                <button className="btn" type="button" onClick={() => testVolumeBeep()}>
                  Test Ton
                </button>
                <button className="btn" type="button" onClick={() => void fireAllDebug()}>
                  Alles feuern
                </button>
              </div>
            </div>
            <div className="hint">Kategorien aktivieren und Timer pro Kategorie konfigurieren.</div>
            {error ? <div className="error">Fehler: {error}</div> : null}
          </div>
        </div>

        {orderedTypes.map((type) => {
          const category = settings.categories[type];
          const next = nextByType ? nextByType[type] : null;
          const nextStartMs = next ? new Date(next.startTime).getTime() : null;
          const countdown = nextStartMs ? formatCountdown(nextStartMs - now) : "—";
          const timeLabel = next ? formatLocalTime(next.startTime) : "—";
          const name = getEventName(type, next);
          const spokenName = getSpokenEventName(type, next);

          return (
            <div className={`card span12 categoryCard ${type}`} key={type}>
              <h2>{typeLabel(type)}</h2>
              <div className="form">
                <div className="row">
                  <div className="kpi">
                    <div className="label">{name}</div>
                    <div className="value" style={{ fontSize: 18 }}>
                      {countdown}
                    </div>
                  </div>
                  <div className="categoryRight">
                    <div className="pill">{timeLabel}</div>
                    <label className="toggle">
                      <input type="checkbox" checked={category.enabled} onChange={(e) => setCategoryEnabled(type, e.target.checked)} />
                      <span className="toggleLabel">Erinnern</span>
                    </label>
                  </div>
                </div>

                {category.enabled ? (
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

                    {Array.from({ length: category.timerCount }).map((_, i) => {
                      const timer = category.timers[i];

                      return (
                        <div className="level" key={i}>
                          <div className="levelHeader">
                            <div className="levelTitle">{`Timer ${i + 1}`}</div>
                            <button
                              className="btn"
                              type="button"
                              onClick={() => {
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
                            <div className="field">
                              <label>
                                Minuten vorher: <span className="pill">{timer.minutesBefore} min</span>
                              </label>
                              <input
                                type="range"
                                min={1}
                                max={60}
                                step={1}
                                value={timer.minutesBefore}
                                onChange={(e) => updateTimer(type, i, { minutesBefore: clampInt(Number(e.target.value), 1, 60) })}
                              />
                            </div>

                            <div className="timerRow">
                              <label className="toggle">
                                <input
                                  type="checkbox"
                                  checked={timer.ttsEnabled}
                                  onChange={(e) => updateTimer(type, i, { ttsEnabled: e.target.checked })}
                                />
                                <span className="toggleLabel">TTS</span>
                              </label>

                              <div className="field" style={{ margin: 0 }}>
                                <label>Beep</label>
                                <select
                                  className="select"
                                  value={timer.beepPattern}
                                  onChange={(e) => updateTimer(type, i, { beepPattern: e.target.value as BeepPattern })}
                                >
                                  <option value="beep">Beep</option>
                                  <option value="double">Double Beep</option>
                                  <option value="triple">Triple Beep</option>
                                </select>
                              </div>
                            </div>

                            <div className="field">
                              <label className="pitchRow">
                                Tonhöhe: <span className="pill">{timer.pitchHz} Hz</span>
                                <button
                                  className="iconBtn"
                                  type="button"
                                  aria-label="Ton testen"
                                  title="Ton testen"
                                  onClick={() => playBeep(timer.beepPattern, timer.pitchHz, settings.volume)}
                                >
                                  <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true">
                                    <path
                                      fill="currentColor"
                                      d="M3 10v4c0 .55.45 1 1 1h3l4 4V5L7 9H4c-.55 0-1 .45-1 1zm13.5 2c0-1.77-1.02-3.29-2.5-4.03v8.05c1.48-.73 2.5-2.25 2.5-4.02zM14 3.23v2.06c2.89.86 5 3.54 5 6.71s-2.11 5.85-5 6.71v2.06c4.01-.91 7-4.49 7-8.77s-2.99-7.86-7-8.77z"
                                    />
                                  </svg>
                                </button>
                              </label>
                              <input
                                type="range"
                                min={120}
                                max={2000}
                                step={10}
                                value={timer.pitchHz}
                                onChange={(e) => updateTimer(type, i, { pitchHz: clampInt(Number(e.target.value), 120, 2000) })}
                              />
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
    </div>
  );
}
