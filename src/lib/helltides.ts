import type {
  HelltideScheduleItem,
  LegionScheduleItem,
  ScheduleResponse,
  WorldBossScheduleItem
} from "./types";

// Source: helltides.com/schedule/_payload.json, verified 2026-09-16.
// The public Blizzard developer portal currently documents no Diablo IV event API.
export const HELLTIDE_INTERVAL_SECONDS = 60 * 60;
export const HELLTIDE_DURATION_SECONDS = 55 * 60;
export const LEGION_INTERVAL_SECONDS = 25 * 60;
export const WORLD_BOSS_INTERVAL_SECONDS = 3.5 * 60 * 60;
export const SCHEDULE_HORIZON_SECONDS = 48 * 60 * 60;

export const LEGION_ANCHOR_SECONDS = 1_789_588_200; // 2026-09-16T19:50:00Z
export const WORLD_BOSS_ANCHOR_SECONDS = 1_789_588_800; // 2026-09-16T20:00:00Z

function makeItem<T extends "helltide" | "legion" | "world_boss">(type: T, timestamp: number) {
  return {
    id: timestamp,
    timestamp,
    startTime: new Date(timestamp * 1000).toISOString(),
    type
  } as T extends "helltide"
    ? HelltideScheduleItem
    : T extends "legion"
      ? LegionScheduleItem
      : WorldBossScheduleItem;
}

function firstOccurrence(nowSeconds: number, anchorSeconds: number, intervalSeconds: number, includeCurrent: boolean): number {
  const offset = (nowSeconds - anchorSeconds) / intervalSeconds;
  const steps = Math.max(0, includeCurrent ? Math.floor(offset) : Math.ceil(offset));
  return anchorSeconds + steps * intervalSeconds;
}

function buildOccurrences<T extends "helltide" | "legion" | "world_boss">(
  type: T,
  nowSeconds: number,
  anchorSeconds: number,
  intervalSeconds: number,
  horizonSeconds: number,
  includeCurrent: boolean
) {
  const endSeconds = nowSeconds + horizonSeconds;
  const items: Array<ReturnType<typeof makeItem<T>>> = [];
  for (
    let timestamp = firstOccurrence(nowSeconds, anchorSeconds, intervalSeconds, includeCurrent);
    timestamp <= endSeconds;
    timestamp += intervalSeconds
  ) {
    items.push(makeItem(type, timestamp));
  }
  return items;
}

/** Generate deterministic event times from the current wall clock. */
export function generateSchedule(nowMs = Date.now(), horizonSeconds = SCHEDULE_HORIZON_SECONDS): ScheduleResponse {
  const safeNowMs = Number.isFinite(nowMs) ? nowMs : Date.now();
  const nowSeconds = Math.floor(safeNowMs / 1000);

  return {
    helltide: buildOccurrences("helltide", nowSeconds, 0, HELLTIDE_INTERVAL_SECONDS, horizonSeconds, true),
    legion: buildOccurrences("legion", nowSeconds, LEGION_ANCHOR_SECONDS, LEGION_INTERVAL_SECONDS, horizonSeconds, false),
    world_boss: buildOccurrences(
      "world_boss",
      nowSeconds,
      WORLD_BOSS_ANCHOR_SECONDS,
      WORLD_BOSS_INTERVAL_SECONDS,
      horizonSeconds,
      false
    )
  };
}
