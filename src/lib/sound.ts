import type { BeepPattern } from "./settings";

function getAudioContext(): AudioContext | null {
  const AudioContextCtor = (window.AudioContext || (window as any).webkitAudioContext) as typeof AudioContext | undefined;
  if (!AudioContextCtor) return null;
  try {
    return new AudioContextCtor();
  } catch {
    return null;
  }
}

function scheduleBeep(ctx: AudioContext, startAt: number, frequency: number, durationMs: number, gainValue: number) {
  const osc = ctx.createOscillator();
  const gain = ctx.createGain();

  const duration = durationMs / 1000;

  osc.type = "sine";
  osc.frequency.value = frequency;

  gain.gain.setValueAtTime(0, startAt);
  gain.gain.linearRampToValueAtTime(gainValue, startAt + 0.01);
  gain.gain.linearRampToValueAtTime(0, startAt + Math.max(0.02, duration - 0.01));

  osc.connect(gain);
  gain.connect(ctx.destination);

  osc.start(startAt);
  osc.stop(startAt + duration);
}

export function playBeep(pattern: BeepPattern, pitchHz: number, volume = 1): number {
  const ctx = getAudioContext();
  if (!ctx) return 0;

  const frequency = Number.isFinite(pitchHz) ? Math.max(120, Math.min(2000, Math.round(pitchHz))) : 880;
  const vol = Number.isFinite(volume) ? Math.max(0, Math.min(1, volume)) : 1;
  if (vol <= 0) return 0;
  const gainValue = 0.08 * vol;
  const durationMs = 170;
  const gapMs = 140;

  const count = pattern === "triple" ? 3 : pattern === "double" ? 2 : 1;
  const start = ctx.currentTime + 0.01;

  for (let i = 0; i < count; i++) {
    const offset = (i * (durationMs + gapMs)) / 1000;
    scheduleBeep(ctx, start + offset, frequency, durationMs, gainValue);
  }

  const totalMs = count * durationMs + Math.max(0, count - 1) * gapMs + 50;
  window.setTimeout(() => {
    ctx.close().catch(() => {});
  }, totalMs);

  return totalMs;
}
