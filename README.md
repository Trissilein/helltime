# helltime

Desktop-App (später optional Android) als schlanker **Reminder** für Diablo 4 Helltide-Events.

Datenquelle: `https://helltides.com/api/schedule` (wird über das Tauri-Backend abgefragt, damit es keine CORS-Probleme gibt).

## Stack

- UI: React + Vite + TypeScript (responsive)
- Desktop: Tauri (Rust) + `invoke` Commands

## Verhalten

- Fenster ist als kleines Widget gedacht.
- Benachrichtigungen optional als **System-Notifications** (Windows Toasts; Position bestimmt das OS).

## UI (aktuell)

1. **Intro & Settings** (Status, Refresh, System-Toasts/Lautstärke)
2. **Reminder**: 3 Kategorien (Helltide / Legion / World Boss)
   - Toggle **Erinnern** schaltet pro Kategorie (keine Einzel-Events)
   - Pro Kategorie: **Timer Anzahl** (1–3)
   - Pro Timer: Minuten vorher (1–60), TTS, Beep-Pattern (beep/double/triple), Tonhöhe (Hz)
   - Test-Button pro Timer (Beep + optional TTS)
   - Kategorien sortieren sich automatisch nach dem nächsten Start (frühestes oben); deaktivierte Kategorien klappen ein

## Voraussetzungen

- Node.js >= 20 + npm
- Rust stable (inkl. `cargo`)
- Tauri System-Dependencies (je nach OS): https://tauri.app/start/prerequisites/

## Lokale Entwicklung

```bash
npm install
npm run tauri dev
```

## Build

```bash
npm run tauri build
```

## Nächste Schritte (Android)

Tauri Mobile ist (je nach Tauri-Version) als nächster Schritt möglich; sobald du soweit bist, sag kurz Bescheid, dann verdrahten wir das Projekt für Android (Gradle, Permissions, Notification-Verhalten, etc.).
