# helltime

`helltime` ist ein Desktop-Reminder fuer Diablo 4 Events.  
Die App zeigt kommende Helltide-, Legion- und World-Boss-Termine mit konfigurierbaren Timern und Overlay-Benachrichtigungen.

Datenquelle: `https://helltides.com/api/schedule`

## Download (Windows)

- Setup-Installer (EXE): https://github.com/Trissilein/helltime/releases/latest/download/helltime-setup-x64.exe
- MSI-Installer: https://github.com/Trissilein/helltime/releases/latest/download/helltime-installer-x64.msi
- SHA-256 Checksums: https://github.com/Trissilein/helltime/releases/latest/download/SHA256SUMS.txt

Hinweis: Die Installer sind aktuell nicht code-signiert. Windows SmartScreen kann beim ersten Start eine Warnung anzeigen.

PowerShell-Beispiel zur Hash-Pruefung:

```powershell
Invoke-WebRequest -Uri "https://github.com/Trissilein/helltime/releases/latest/download/SHA256SUMS.txt" -OutFile ".\SHA256SUMS.txt"
Get-FileHash ".\helltime-setup-x64.exe" -Algorithm SHA256
Get-FileHash ".\helltime-installer-x64.msi" -Algorithm SHA256
Get-Content ".\SHA256SUMS.txt"
```

## Was helltime kann

- Event-Timer fuer Helltide, Legion und World Boss mit frei waehlbaren Vorwarnzeiten.
- Overlay-Benachrichtigungen als dauerhafte Overview oder als Toast-Ansicht.
- Pro Kategorie konfigurierbare Reminder (TTS-Name, Beep-Pattern, Timing, Tonhoehe).
- Automatischer Daten-Refresh in regelmaessigen Intervallen.

## Voraussetzungen

- Node.js >= 20 und npm
- Rust stable (inkl. `cargo`)
- Tauri System-Dependencies: https://tauri.app/start/prerequisites/

## Lokale Entwicklung

```bash
npm install
npm run tauri dev
```

## Build

```bash
npm run tauri build
```

## Kurzes Troubleshooting

- Wenn in der Konsole `not allowed` erscheint, pruefe `src-tauri/capabilities/default.json`.
- Das Overlay ist standardmaessig click-through und wird beim Schliessen des Main-Fensters beendet.
- Bei fehlerhafter Overlay-Position die `Reset`-Funktion in den Benachrichtigungseinstellungen verwenden.

## Roadmap

Der aktuelle Fokus liegt auf der Desktop-App; Android bleibt eine optionale spaetere Erweiterung.

## English (Short)

- `helltime` is a desktop reminder for Diablo 4 Helltide, Legion, and World Boss events.
- Download links: EXE https://github.com/Trissilein/helltime/releases/latest/download/helltime-setup-x64.exe, MSI https://github.com/Trissilein/helltime/releases/latest/download/helltime-installer-x64.msi, Checksums https://github.com/Trissilein/helltime/releases/latest/download/SHA256SUMS.txt
- Data source: `https://helltides.com/api/schedule`
- Local dev: `npm install` then `npm run tauri dev`
- Build: `npm run tauri build`
- Platform focus is desktop; Android may be explored later.
