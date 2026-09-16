# helltime

`helltime` is a Windows desktop overlay and reminder for Diablo IV open-world events.
It focuses on upcoming **Helltide**, **Legion**, and **World Boss** timings with configurable reminders, readable in-game overlay lines, and fast access to the live Helltides map.

Event times are generated locally from documented cadence rules. The app does not fetch a timer feed at runtime.

## What It Does

- Tracks the next Helltide, Legion, and World Boss events.
- Shows reminders in the main app and in an always-on-top overlay.
- Supports multiple timers per event with TTS, beep pattern, and pitch controls.
- Lets you tune overlay size, background transparency, and line-background transparency separately.
- Shows generic World Boss timing; names and locations are omitted because they do not have a stable public cadence rule.
- Opens the Helltides live map directly from the Helltide card.

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

## Diablo IV Context

Verified against official Blizzard sources on **May 9, 2026**:

- [Vessel of Hatred](https://diablo4.blizzard.com/en-gb/vessel-of-hatred) remains Diablo IV's first expansion and introduced **Spiritborn**, **Nahantu**, **Mercenaries**, **Runewords**, **Party Finder**, and **Dark Citadel**.
- Blizzard's official [2025 Diablo IV roadmap](https://news.blizzard.com/en-us/article/24189529/the-age-of-hatred-persists-diablo-iv-2025-roadmap) states that the game is moving toward its **second expansion in 2026**.
- The official [Lord of Hatred page](https://diablo4.blizzard.com/es-es/lord-of-hatred) lists it as available **April 28, 2026** and describes **Skovos**, major class/system updates, and inclusion of **Vessel of Hatred**.

`helltime` itself does **not** depend on expansion ownership. Its local cadence rules keep core reminders useful across base-game and expansion eras.

## Event Schedule Notes

- Cadence rules were verified against the public `helltides.com` schedule payload on **2026-09-16**.
- Helltide starts at each UTC hour and remains active for 55 minutes.
- Legion starts every 25 minutes from the verified UTC anchor `2026-09-16T19:50:00Z`.
- World Boss starts every 3.5 hours from the verified UTC anchor `2026-09-16T20:00:00Z`.
- The app opens **helltides.com** only when you use the optional map button; the timer itself has no network dependency.
- If Blizzard changes cadence or resets an event phase, update the constants in `src/lib/helltides.ts`.

## License

This project is released under the [MIT License](LICENSE).

For packaged dependencies and attribution notes, see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Acknowledgements

Schedule cadence provenance: **helltides.com** public schedule, verified 2026-09-16.

`helltime` also builds on:

- `tauri-apps/tauri`
- `react` / `react-dom`
- `vite`

`Diablo`, `Diablo IV`, `Vessel of Hatred`, and `Lord of Hatred` are trademarks of Blizzard Entertainment, Inc.  
`helltime` is an independent fan-made utility and is not affiliated with or endorsed by Blizzard Entertainment.

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

## Aktueller Stand

Die aktuelle Version fokussiert sich auf ein lesbareres Overlay, aufgeraeumte Konfiguration und schnelleren Zugriff auf relevante Event-Kontexte.

## English (Short)

- `helltime` is a desktop overlay/reminder for Diablo IV Helltide, Legion, and World Boss events.
- Download links: EXE https://github.com/Trissilein/helltime/releases/latest/download/helltime-setup-x64.exe, MSI https://github.com/Trissilein/helltime/releases/latest/download/helltime-installer-x64.msi, Checksums https://github.com/Trissilein/helltime/releases/latest/download/SHA256SUMS.txt
- Event times: locally generated from cadence rules; map link: https://helltides.com/
- License: MIT
- Developer setup only: `npm install` then `npm run tauri dev`
- Build: `npm run tauri build`
- Current focus: clearer overlay readability, cleaner configuration, and faster event context access.
