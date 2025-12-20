# helltime

Desktop-App (später optional Android) als schlanker **Reminder** für Diablo 4 Helltide-Events.

Datenquelle: `https://helltides.com/api/schedule` (wird über das Tauri-Backend abgefragt, damit es keine CORS-Probleme gibt).

## Stack

- UI: React + Vite + TypeScript (responsive)
- Desktop: Tauri (Rust) + `invoke` Commands

## Verhalten

- Fenster ist als kleines Widget gedacht.
- Benachrichtigungen laufen über ein eigenes **Overlay-Fenster** (always-on-top), entweder als permanente Übersicht oder als Toast-Mode.
- Auto-Refresh ist immer aktiv (randomisiert zwischen 10–15 Minuten).

## UI (aktuell)

1. **Event Panels**: 3 Kategorien (Helltide / Legion / World Boss)
   - Toggle **Erinnern** schaltet pro Kategorie (keine Einzel-Events)
   - Pro Kategorie: optionaler **TTS-Name** (World Boss: `{boss}` Platzhalter)
   - Pro Kategorie: **Timer Anzahl** (1–3)
   - Pro Timer: Minuten vorher (1–60), TTS, Beep-Pattern (beep/double/triple), Tonhöhe (Hz)
   - Test-Button pro Timer (Beep + optional TTS)
   - Kategorien sortieren sich automatisch nach dem nächsten Start (frühestes oben); deaktivierte Kategorien klappen ein

2. **Einstellungen** (Zahnrad oben rechts)
   - Status, Overlay/Lautstärke und Benachrichtigungs-Optionen

3. **Overlay** (Benachrichtigungen)
   - Overlay **an/aus**
   - Mode **Overview** oder **Toast**
   - Kategorienauswahl für die Overview-Liste
   - Hintergrundfarbe, Transparenz, Skalierung

## Voraussetzungen

- Node.js >= 20 + npm
- Rust stable (inkl. `cargo`)
- Tauri System-Dependencies (je nach OS): https://tauri.app/start/prerequisites/

## Lokale Entwicklung

```bash
npm install
npm run tauri dev
```

## Tauri Capabilities (Overlay / Window / Events / Shell)

Tauri v2 schützt einige Core-APIs (z.B. `window.get_all_windows`, `event.emit`, `shell.open`) per Capabilities.
Falls Overlay/Einstellungen „nichts machen“ oder die Konsole `not allowed` meldet, prüfe `src-tauri/capabilities/default.json`.

## Overlay “klemmt” / blockiert Eingaben?

- Das Overlay ist standardmäßig **click-through** (ignoriert Cursor Events) und wird beim Schließen des Main-Windows automatisch beendet.
- Beim Schließen des Main-Fensters beendet sich die App jetzt immer komplett (auch wenn das Overlay offen ist).
- Falls du aus einer alten Session komische Größen/Positionen “geerbt” hast: im UI unter Benachrichtigungen `Reset` drücken (setzt Overlay Bounds zurück).

## Build

```bash
npm run tauri build
```

## Nächste Schritte (Android)

Tauri Mobile ist (je nach Tauri-Version) als nächster Schritt möglich; sobald du soweit bist, sag kurz Bescheid, dann verdrahten wir das Projekt für Android (Gradle, Permissions, Notification-Verhalten, etc.).
