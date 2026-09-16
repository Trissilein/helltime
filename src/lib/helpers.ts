import { HELLTIDE_DURATION_SECONDS } from "./helltides";

export const HELLTIDE_DURATION_MS = HELLTIDE_DURATION_SECONDS * 1000;
export const REMINDER_FIRE_WINDOW_MS = 30_000;
export const REMINDER_CATCH_UP_WINDOW_MS = 5 * 60_000;

export type EventDisplayTiming<T> = {
  item: T;
  startMs: number;
  targetMs: number;
  active: boolean;
};

function getStartMs(item: { startTime: string }): number {
  return new Date(item.startTime).getTime();
}

/**
 * Find the next upcoming event from a list of items with startTime
 * @param items Array of items with startTime property
 * @param now Current timestamp in milliseconds
 * @returns The earliest upcoming item, or null if none found
 */
export function findNext<T extends { startTime: string }>(
  items: T[],
  now: number
): T | null {
  let best: T | null = null;
  let bestStartMs = Number.POSITIVE_INFINITY;

  for (const item of items) {
    const startMs = getStartMs(item);
    if (!Number.isFinite(startMs)) continue;
    if (startMs <= now) continue;
    if (startMs < bestStartMs) {
      best = item;
      bestStartMs = startMs;
    }
  }

  return best;
}

export function toUpcomingTiming<T extends { startTime: string }>(item: T | null): EventDisplayTiming<T> | null {
  if (!item) return null;
  const startMs = getStartMs(item);
  if (!Number.isFinite(startMs)) return null;
  return {
    item,
    startMs,
    targetMs: startMs,
    active: false
  };
}

export function findActiveOrNextHelltide<T extends { startTime: string }>(
  items: T[],
  now: number
): EventDisplayTiming<T> | null {
  let next: EventDisplayTiming<T> | null = null;

  for (const item of items) {
    const startMs = getStartMs(item);
    if (!Number.isFinite(startMs)) continue;

    const endMs = startMs + HELLTIDE_DURATION_MS;
    if (startMs <= now && now < endMs) {
      return {
        item,
        startMs,
        targetMs: endMs,
        active: true
      };
    }

    if (startMs > now && (!next || startMs < next.startMs)) {
      next = {
        item,
        startMs,
        targetMs: startMs,
        active: false
      };
    }
  }

  return next;
}

export type DueReminderTimer = {
  index: number;
  triggerMs: number;
};

export type DueReminderResult = {
  due: DueReminderTimer[];
  catchUp: boolean;
};

/** Find reminder thresholds crossed since the previous wall-clock observation. */
export function findDueReminderTimers(
  item: { startTime: string },
  timers: ReadonlyArray<{ minutesBefore: number }>,
  timerCount: number,
  now: number,
  previousNow: number | null
): DueReminderResult {
  const startMs = getStartMs(item);
  if (!Number.isFinite(startMs) || startMs <= now || previousNow === null || now < previousNow) {
    return { due: [], catchUp: false };
  }

  const catchUp = now - previousNow > REMINDER_FIRE_WINDOW_MS;
  const due: DueReminderTimer[] = [];
  for (let index = 0; index < Math.min(timerCount, timers.length); index++) {
    const minutesBefore = timers[index]?.minutesBefore;
    if (!Number.isFinite(minutesBefore)) continue;
    const triggerMs = startMs - minutesBefore * 60_000;
    if (triggerMs <= previousNow || triggerMs > now) continue;
    if (catchUp && now - triggerMs > REMINDER_CATCH_UP_WINDOW_MS) continue;
    due.push({ index, triggerMs });
  }
  return { due, catchUp };
}
