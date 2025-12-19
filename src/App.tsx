import React, { useEffect, useMemo, useRef, useState } from "react";
import { fetchSchedule } from "./lib/helltides";
import { formatCountdown, formatLocalTime } from "./lib/time";
import { loadSettings, saveSettings, type BeepPattern, type Settings, type TimerSettings } from "./lib/settings";
import { playBeep } from "./lib/sound";
import { formatRemainingSpeech, speak } from "./lib/speech";
import type { ScheduleResponse, ScheduleType, WorldBossScheduleItem } from "./lib/types";
import { overlayEnterConfig, overlayExitConfig, overlayGetPosition, overlayShow } from "./lib/overlay";
import { openExternalUrl } from "./lib/external";
import { disablePanicStop, isPanicStopEnabled } from "./lib/safety";

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
  const [overlaySavedAt, setOverlaySavedAt] = useState<number | null>(null);
  const [nextAutoRefreshAt, setNextAutoRefreshAt] = useState<number | null>(null);
  const autoRefreshTimeoutRef = useRef<number | null>(null);
  const [notificationsOpen, setNotificationsOpen] = useState(false);
  const [categoryOpen, setCategoryOpen] = useState<Record<ScheduleType, boolean>>(() => ({
    helltide: false,
    legion: false,
    world_boss: false
  }));
  const [panicStopEnabled, setPanicStopEnabled] = useState(() => isPanicStopEnabled());

  const [settings, setSettings] = useState<Settings>(() => loadSettings());
  const firedRef = useRef<FiredMap>(loadFired());

  useEffect(() => {
    const id = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(id);
  }, []);

  useEffect(() => {
    const onPanic = (e: Event) => {
      const enabled = Boolean((e as CustomEvent).detail?.enabled);
      setPanicStopEnabled(enabled);
    };
    window.addEventListener("helltime:panic-stop", onPanic);
    return () => window.removeEventListener("helltime:panic-stop", onPanic);
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
    if (panicStopEnabled || !settings.autoRefreshEnabled) {
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
  }, [settings.autoRefreshEnabled, panicStopEnabled]);

  useEffect(() => {
    if (panicStopEnabled || !settings.autoRefreshEnabled) return;
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
    if (!nextByType) return [...types];
    return [...types]
      .map((type) => {
        const next = nextByType[type];
        const startMs = next ? new Date(next.startTime).getTime() : Number.POSITIVE_INFINITY;
        return { type, startMs };
      })
      .sort((a, b) => {
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
    if (!enabled) setCategoryOpen((s) => ({ ...s, [type]: false }));
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

  function hexToRgbInt(hex: string): number | null {
    const s = (hex ?? "").trim();
    const m = /^#([0-9a-fA-F]{6})$/.exec(s);
    if (!m) return null;
    return parseInt(m[1], 16);
  }

  async function showOverlayToast(payload: { title: string; body: string; type?: ScheduleType; kind?: "event" | "debug" }) {
    if (panicStopEnabled) return;
    if (!settings.overlayToastsEnabled) return;
    const bg = hexToRgbInt(settings.overlayBgHex);
    await overlayShow(
      { title: payload.title, body: payload.body, kind: payload.kind, type: payload.type, bg_rgb: bg ?? undefined },
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
    setOverlaySavedAt(Date.now());
  }

  function testVolumeBeep(volumeOverride?: number): void {
    if (panicStopEnabled) return;
    if (!settings.soundEnabled) return;
    const sampleTimer = settings.categories.helltide.timers[0];
    playBeep(sampleTimer.beepPattern, sampleTimer.pitchHz, typeof volumeOverride === "number" ? volumeOverride : settings.volume);
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
              <label className="toggle">
                <input
                  type="checkbox"
                  disabled={panicStopEnabled}
                  checked={settings.autoRefreshEnabled}
                  onChange={(e) => setSettings((s) => ({ ...s, autoRefreshEnabled: e.target.checked }))}
                />
                <span className="toggleLabel">Auto</span>
              </label>
              <button className="btn primary" type="button" onClick={() => void openExternalUrl("https://helltides.com")}>
                Quelle
              </button>
            </div>
            <div className="subNote">
              Letztes Update: {lastRefreshAt ? formatClock(lastRefreshAt) : "—"}
              {settings.autoRefreshEnabled && nextAutoRefreshAt ? ` • Auto in ${formatCountdown(nextAutoRefreshAt - now)}` : ""}
            </div>
          </div>
        </div>
      </div>

      <div className="grid">
        <div className="card span12">
          <h2>Intro & Settings</h2>
          <div className="form">
            {panicStopEnabled ? (
              <div className="warning">
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
            <div className="inline">
              <div className="hint">Nächstes Event (aktivierte Kategorien)</div>
              {(() => {
                const label = nextEnabledOverall ? `${nextEnabledOverall.name} • ${formatLocalTime(nextEnabledOverall.startTime)}` : "—";
                return (
                  <div className="pill pillClamp" title={label}>
                    {label}
                  </div>
                );
              })()}
            </div>
            <div className={`section ${notificationsOpen ? "open" : ""}`}>
              <div className="panelHeaderRow">
                <button
                  className="panelHeaderBtn"
                  type="button"
                  aria-expanded={notificationsOpen}
                  onClick={() => setNotificationsOpen((v) => !v)}
                >
                  <span className="panelHeaderTitle">Benachrichtigungen</span>
                </button>
                <div className="panelHeaderRight">
                  <div className="pill small">Overlay + Ton</div>
                </div>
              </div>
              {notificationsOpen ? (
                <div className="sectionBody">
                  <div className="inline">
                    <div className="hint">Overlay Toast (Topmost)</div>
                    <label className="toggle">
                      <input
                        type="checkbox"
                        disabled={panicStopEnabled}
                        checked={settings.overlayToastsEnabled}
                        onChange={(e) => setSettings((s) => ({ ...s, overlayToastsEnabled: e.target.checked }))}
                      />
                      <span className="toggleLabel">{settings.overlayToastsEnabled ? "an" : "aus"}</span>
                    </label>
                  </div>

                  <div className="inline">
                    <div className="hint">Ton</div>
                    <label className="toggle">
                      <input
                        type="checkbox"
                        disabled={panicStopEnabled}
                        checked={settings.soundEnabled}
                        onChange={(e) => setSettings((s) => ({ ...s, soundEnabled: e.target.checked }))}
                      />
                      <span className="toggleLabel">{settings.soundEnabled ? "an" : "aus"}</span>
                    </label>
                  </div>

                <div className="inline">
                  <div className="hint">Overlay Hintergrund</div>
                  <div className="actions">
                    <input
                      type="color"
                      value={settings.overlayBgHex}
                      onChange={(e) => setSettings((s) => ({ ...s, overlayBgHex: e.target.value }))}
                      title="Overlay Hintergrund"
                    />
                    <div className="pill">{settings.overlayBgHex}</div>
                    <button className="btn" type="button" onClick={() => void startOverlayPositionMode()}>
                      Position
                    </button>
                <button className="btn" type="button" onClick={() => void saveOverlayPosition()}>
                  Speichern
                </button>
                <button
                  className="btn"
                  type="button"
                  disabled={panicStopEnabled}
                  onClick={() => void showOverlayToast({ title: "helltime", body: "Overlay Toast Test", kind: "debug" })}
                >
                  Test
                </button>
              </div>
            </div>

                {overlaySavedAt ? <div className="hint">Gespeichert: {formatClock(overlaySavedAt)}</div> : null}
                <div className="field">
                  <label>
                    Lautstärke: <span className="pill">{Math.round(settings.volume * 100)}%</span>
                  </label>
                  <input
                    type="range"
                    min={0}
                    max={100}
                    step={1}
                    disabled={!settings.soundEnabled}
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
                </div>
              ) : null}
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

          const isOpen = category.enabled && categoryOpen[type];

          return (
            <div className={`card span12 categoryCard ${type} categoryDetails ${isOpen ? "open" : ""}`} key={type}>
              <div className="panelHeaderRow categoryHeader">
                <button
                  className="panelHeaderBtn categoryExpandBtn"
                  type="button"
                  disabled={!category.enabled}
                  aria-expanded={isOpen}
                  onClick={() => {
                    if (!category.enabled) return;
                    setCategoryOpen((s) => ({ ...s, [type]: !s[type] }));
                  }}
                >
                  <span className="panelHeaderTitle" title={name}>
                    {name}
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
