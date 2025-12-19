const KEY = "helltime:overlayDiag";
const MAX = 200;

export type OverlayDiagEntry = { ts: number; msg: string };

export function pushOverlayDiag(msg: string): void {
  try {
    const raw = localStorage.getItem(KEY);
    const items: OverlayDiagEntry[] = raw ? (JSON.parse(raw) as OverlayDiagEntry[]) : [];
    items.push({ ts: Date.now(), msg });
    while (items.length > MAX) items.shift();
    localStorage.setItem(KEY, JSON.stringify(items));
  } catch {
    // ignore
  }
}

export function readOverlayDiag(): OverlayDiagEntry[] {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw) as OverlayDiagEntry[];
    if (!Array.isArray(parsed)) return [];
    return parsed.filter((e) => typeof e?.ts === "number" && typeof e?.msg === "string");
  } catch {
    return [];
  }
}

export function clearOverlayDiag(): void {
  try {
    localStorage.removeItem(KEY);
  } catch {
    // ignore
  }
}

