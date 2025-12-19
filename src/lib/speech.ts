export function formatRemainingSpeech(msRemaining: number): string {
  const totalSeconds = Math.max(0, Math.floor(msRemaining / 1000));
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;

  const parts: string[] = [];
  if (hours > 0) parts.push(`${hours} ${hours === 1 ? "Stunde" : "Stunden"}`);
  if (minutes > 0) parts.push(`${minutes} ${minutes === 1 ? "Minute" : "Minuten"}`);

  if (hours === 0 && minutes === 0) {
    return `${seconds} ${seconds === 1 ? "Sekunde" : "Sekunden"}`;
  }

  // Sekunden nur anhängen, wenn es wirklich kurz ist (klingt sonst nervig)
  if (hours === 0 && minutes <= 1 && seconds > 0) {
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
