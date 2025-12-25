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
