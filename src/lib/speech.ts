// German approximation words for natural-sounding announcements
const APPROXIMATION_WORDS = [
  "ungefähr",
  "circa",
  "etwa",
  "knapp",
  "rund",
  "zirka",
  "in etwa",
  "schätzungsweise",
  "annähernd",
  "vergleichsweise"
];

function getRandomApproximation(): string {
  return APPROXIMATION_WORDS[Math.floor(Math.random() * APPROXIMATION_WORDS.length)];
}

export function formatRemainingSpeech(msRemaining: number): string {
  const totalSeconds = Math.max(0, Math.floor(msRemaining / 1000));

  // Round to nearest minute for more natural announcements
  // (4:57 → 5 minutes, not 4 minutes)
  const totalMinutes = Math.round(totalSeconds / 60);
  const hours = Math.floor(totalMinutes / 60);
  const minutes = totalMinutes % 60;
  const seconds = totalSeconds % 60; // Original seconds for short durations

  const parts: string[] = [];

  // Add approximation word for longer durations (minutes or hours)
  if (hours > 0 || minutes > 0) {
    parts.push(getRandomApproximation());
  }

  if (hours > 0) parts.push(`${hours} ${hours === 1 ? "Stunde" : "Stunden"}`);
  if (minutes > 0) parts.push(`${minutes} ${minutes === 1 ? "Minute" : "Minuten"}`);

  // When very short (< 1 minute rounded), announce seconds
  if (hours === 0 && totalMinutes === 0) {
    return `${seconds} ${seconds === 1 ? "Sekunde" : "Sekunden"}`;
  }

  // Sekunden nur anhängen, wenn es wirklich kurz ist (klingt sonst nervig)
  if (hours === 0 && totalMinutes <= 1 && seconds > 0) {
    parts.push(`${seconds} ${seconds === 1 ? "Sekunde" : "Sekunden"}`);
  }

  return parts.join(" ");
}

export async function speak(text: string, volume = 1): Promise<void> {
  if (!("speechSynthesis" in window)) return;

  try {
    const utterance = new SpeechSynthesisUtterance(text);
    utterance.lang = navigator.language || "de-DE";
    utterance.volume = Number.isFinite(volume) ? Math.max(0, Math.min(1, volume)) : 1;

    // Cancel queue to keep it "snappy"
    window.speechSynthesis.cancel();

    await new Promise<void>((resolve) => {
      utterance.onend = () => resolve();
      utterance.onerror = () => resolve();
      window.speechSynthesis.speak(utterance);
    });
  } catch {
    // ignore
  }
}
