import type { ScheduleType } from "./types";
import { readJson, writeJson } from "./storage";

export type BeepPattern = "beep" | "double" | "triple";

export type TimerSettings = {
  minutesBefore: number; // 1-60
  ttsEnabled: boolean;
  beepPattern: BeepPattern;
  pitchHz: number; // ~120-2000
};

export type CategorySettings = {
  enabled: boolean;
  timerCount: 1 | 2 | 3;
  timers: [TimerSettings, TimerSettings, TimerSettings];
};

export type Settings = {
  version: 5;
  volume: number; // 0-1
  systemToastsEnabled: boolean;
  overlayToastsEnabled: boolean;
  overlayToastsPosition: { x: number; y: number } | null;
  overlayBgHex: string; // "#rrggbb"
  categories: Record<ScheduleType, CategorySettings>;
};

const STORAGE_KEY = "settings_v5";

function defaultTimers(): [TimerSettings, TimerSettings, TimerSettings] {
  return [
    { minutesBefore: 30, ttsEnabled: true, beepPattern: "beep", pitchHz: 880 },
    { minutesBefore: 10, ttsEnabled: false, beepPattern: "double", pitchHz: 880 },
    { minutesBefore: 5, ttsEnabled: false, beepPattern: "triple", pitchHz: 880 }
  ];
}

function defaultCategory(enabled = true): CategorySettings {
  return {
    enabled,
    timerCount: 3,
    timers: defaultTimers()
  };
}

const defaults: Settings = {
  version: 5,
  volume: 0.8,
  systemToastsEnabled: false,
  overlayToastsEnabled: false,
  overlayToastsPosition: null,
  overlayBgHex: "#0b1220",
  categories: {
    helltide: defaultCategory(true),
    legion: defaultCategory(true),
    world_boss: defaultCategory(true)
  }
};

function normalizeHexColor(raw: any, fallback: string): string {
  if (typeof raw !== "string") return fallback;
  const s = raw.trim().toLowerCase();
  if (/^#[0-9a-f]{6}$/.test(s)) return s;
  return fallback;
}

function normalizePosition(raw: any): { x: number; y: number } | null {
  const x = raw?.x;
  const y = raw?.y;
  if (typeof x !== "number" || typeof y !== "number") return null;
  if (!Number.isFinite(x) || !Number.isFinite(y)) return null;
  return { x: Math.round(x), y: Math.round(y) };
}

function clampUnit(n: unknown, fallback: number): number {
  if (typeof n !== "number" || !Number.isFinite(n)) return fallback;
  return Math.max(0, Math.min(1, n));
}

function clampInt(n: unknown, fallback: number, min: number, max: number): number {
  if (typeof n !== "number" || !Number.isFinite(n)) return fallback;
  return Math.max(min, Math.min(max, Math.round(n)));
}

function normalizeBeepPattern(v: unknown, fallback: BeepPattern): BeepPattern {
  if (v === "beep" || v === "double" || v === "triple") return v;
  return fallback;
}

function normalizeTimer(raw: any, fallback: TimerSettings): TimerSettings {
  return {
    minutesBefore: clampInt(raw?.minutesBefore, fallback.minutesBefore, 1, 60),
    ttsEnabled: typeof raw?.ttsEnabled === "boolean" ? raw.ttsEnabled : fallback.ttsEnabled,
    beepPattern: normalizeBeepPattern(raw?.beepPattern, fallback.beepPattern),
    pitchHz: clampInt(raw?.pitchHz, fallback.pitchHz, 120, 2000)
  };
}

function normalizeCategory(raw: any, fallback: CategorySettings): CategorySettings {
  const timerCountRaw = raw?.timerCount;
  const timerCount =
    timerCountRaw === 1 || timerCountRaw === 2 || timerCountRaw === 3 ? timerCountRaw : fallback.timerCount;

  const rawTimers = Array.isArray(raw?.timers) ? raw.timers : [];
  const timers = [0, 1, 2].map((i) => normalizeTimer(rawTimers[i], fallback.timers[i])) as CategorySettings["timers"];

  return {
    enabled: typeof raw?.enabled === "boolean" ? raw.enabled : fallback.enabled,
    timerCount,
    timers
  };
}

function timersFromV2(levels: any): [TimerSettings, TimerSettings, TimerSettings] {
  const rawLevels = Array.isArray(levels) ? levels : [];
  const defaultsTimers = defaultTimers();

  return [0, 1, 2].map((i) => {
    const raw = rawLevels[i] ?? {};
    const fallback = defaultsTimers[i];

    const sound = raw.sound;
    const beepPattern: BeepPattern = sound === "alarm" ? "triple" : sound === "beep" ? "beep" : fallback.beepPattern;

    return {
      minutesBefore: clampInt(raw.minutesBefore, fallback.minutesBefore, 1, 60),
      ttsEnabled: typeof raw.ttsEnabled === "boolean" ? raw.ttsEnabled : fallback.ttsEnabled,
      beepPattern,
      pitchHz: fallback.pitchHz
    };
  }) as [TimerSettings, TimerSettings, TimerSettings];
}

function cloneTimers(timers: [TimerSettings, TimerSettings, TimerSettings]): [TimerSettings, TimerSettings, TimerSettings] {
  return [{ ...timers[0] }, { ...timers[1] }, { ...timers[2] }];
}

export function loadSettings(): Settings {
  const v4 = readJson<unknown>(STORAGE_KEY, null);
  if (v4 && typeof v4 === "object" && (v4 as any).version === 5) {
    const raw = v4 as any;
    const rawCategories = raw.categories ?? {};

    return {
      version: 5,
      volume: clampUnit(raw.volume, defaults.volume),
      systemToastsEnabled:
        typeof raw.systemToastsEnabled === "boolean"
          ? raw.systemToastsEnabled
          : typeof raw.toastEnabled === "boolean"
            ? raw.toastEnabled
            : defaults.systemToastsEnabled,
      overlayToastsEnabled: typeof raw.overlayToastsEnabled === "boolean" ? raw.overlayToastsEnabled : defaults.overlayToastsEnabled,
      overlayToastsPosition: normalizePosition(raw.overlayToastsPosition),
      overlayBgHex: normalizeHexColor(raw.overlayBgHex, defaults.overlayBgHex),
      categories: {
        helltide: normalizeCategory(rawCategories.helltide, defaults.categories.helltide),
        legion: normalizeCategory(rawCategories.legion, defaults.categories.legion),
        world_boss: normalizeCategory(rawCategories.world_boss, defaults.categories.world_boss)
      }
    };
  }

  const v4raw = readJson<any>("settings_v4", null);
  if (v4raw && typeof v4raw === "object" && v4raw.version === 4) {
    return {
      version: 5,
      volume: clampUnit(v4raw.volume, defaults.volume),
      systemToastsEnabled:
        typeof v4raw.systemToastsEnabled === "boolean" ? v4raw.systemToastsEnabled : defaults.systemToastsEnabled,
      overlayToastsEnabled: typeof v4raw.overlayToastsEnabled === "boolean" ? v4raw.overlayToastsEnabled : defaults.overlayToastsEnabled,
      overlayToastsPosition: normalizePosition(v4raw.overlayToastsPosition),
      overlayBgHex: defaults.overlayBgHex,
      categories: {
        helltide: normalizeCategory(v4raw.categories?.helltide, defaults.categories.helltide),
        legion: normalizeCategory(v4raw.categories?.legion, defaults.categories.legion),
        world_boss: normalizeCategory(v4raw.categories?.world_boss, defaults.categories.world_boss)
      }
    };
  }

  const v3 = readJson<any>("settings_v3", null);
  if (v3 && typeof v3 === "object" && v3.version === 3) {
    return {
      version: 5,
      volume: clampUnit(v3.volume, defaults.volume),
      systemToastsEnabled: typeof v3.systemToastsEnabled === "boolean" ? v3.systemToastsEnabled : defaults.systemToastsEnabled,
      overlayToastsEnabled: defaults.overlayToastsEnabled,
      overlayToastsPosition: defaults.overlayToastsPosition,
      overlayBgHex: defaults.overlayBgHex,
      categories: {
        helltide: normalizeCategory(v3.categories?.helltide, defaults.categories.helltide),
        legion: normalizeCategory(v3.categories?.legion, defaults.categories.legion),
        world_boss: normalizeCategory(v3.categories?.world_boss, defaults.categories.world_boss)
      }
    };
  }

  const v2 = readJson<any>("settings_v2", null);
  if (v2 && typeof v2 === "object" && v2.version === 2) {
    const enabled = v2.eventEnabled ?? {};
    const timerCount = v2.levelCount === 1 || v2.levelCount === 2 || v2.levelCount === 3 ? v2.levelCount : 3;
    const timers = timersFromV2(v2.levels);

    return {
      version: 5,
      volume: defaults.volume,
      systemToastsEnabled: defaults.systemToastsEnabled,
      overlayToastsEnabled: defaults.overlayToastsEnabled,
      overlayToastsPosition: defaults.overlayToastsPosition,
      overlayBgHex: defaults.overlayBgHex,
      categories: {
        helltide: { enabled: typeof enabled.helltide === "boolean" ? enabled.helltide : true, timerCount, timers: cloneTimers(timers) },
        legion: { enabled: typeof enabled.legion === "boolean" ? enabled.legion : true, timerCount, timers: cloneTimers(timers) },
        world_boss: {
          enabled: typeof enabled.world_boss === "boolean" ? enabled.world_boss : true,
          timerCount,
          timers: cloneTimers(timers)
        }
      }
    };
  }

  const v1 = readJson<any>("settings", null);
  if (v1 && typeof v1 === "object") {
    const minutesBefore = clampInt(v1.minutesBefore, 30, 1, 60);
    const enabled = typeof v1.notifyEnabled === "boolean" ? v1.notifyEnabled : true;
    const timers = defaultTimers();
    timers[0] = { ...timers[0], minutesBefore };

    return {
      version: 5,
      volume: defaults.volume,
      systemToastsEnabled: defaults.systemToastsEnabled,
      overlayToastsEnabled: defaults.overlayToastsEnabled,
      overlayToastsPosition: defaults.overlayToastsPosition,
      overlayBgHex: defaults.overlayBgHex,
      categories: {
        helltide: { enabled, timerCount: 1, timers },
        legion: { ...defaultCategory(false) },
        world_boss: { ...defaultCategory(false) }
      }
    };
  }

  return defaults;
}

export function saveSettings(settings: Settings): void {
  writeJson(STORAGE_KEY, settings);
}
