# helltime

`helltime` ist ein Desktop-Reminder fuer Diablo 4 Events.  
Die App zeigt kommende Helltide-, Legion- und World-Boss-Termine mit konfigurierbaren Timern und Overlay-Benachrichtigungen.

Datenquelle: `https://helltides.com/api/schedule`

## Danksagung / Acknowledgements

Ein riesiges Dankeschoen an **helltides.com** fuer die Bereitstellung der Event-Datenquelle,
ohne die diese App in der Form nicht moeglich waere.

`helltime` nutzt ausserdem zentrale Open-Source-Bausteine:

- `tauri-apps/tauri` (Desktop Runtime)
- `react` / `react-dom` (UI)
- `vite` (Build Tooling)

Volle Third-Party- und Lizenzhinweise: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)

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

## Voraussetzungen (nur fuer Development/Contributing)

Als End-User brauchst du keine lokale Toolchain. Fuer die Nutzung reicht der Installer aus dem Download-Abschnitt.

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

## Aktueller Fokus

Aktuell liegt der Fokus auf Interface-Polishing und UX-Feinschliff der Desktop-App.

## English (Short)

- `helltime` is a desktop reminder for Diablo 4 Helltide, Legion, and World Boss events.
- Download links: EXE https://github.com/Trissilein/helltime/releases/latest/download/helltime-setup-x64.exe, MSI https://github.com/Trissilein/helltime/releases/latest/download/helltime-installer-x64.msi, Checksums https://github.com/Trissilein/helltime/releases/latest/download/SHA256SUMS.txt
- Data source: `https://helltides.com/api/schedule`
- Developer setup only: `npm install` then `npm run tauri dev`
- Build: `npm run tauri build`
- Current focus: interface polishing and desktop UX refinements.
