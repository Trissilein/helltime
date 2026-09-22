# Helltime – Native Win32 Handoff

Stand: 2026-09-22  
Repository: `D:\GIT\helltime`  
Basis vor diesem Handoff: `13b490c feat: add native Win32 Helltime client`

## Fortschritt nach Handoff

- `c978691`: UTF-8-Quellen, dunkle Windows-Titelleiste und Per-Monitor-DPI.
- `7adeb9e`: Tray-Checkboxen für Overlay und Erinnerungen wiederhergestellt.
- Overlay-Lifecycle: Toast bleibt ohne Reminder verborgen; Reminder kann Overview
  temporär ersetzen; leere Overview versteckt sich; Tray-Positionieren wartet auf
  echten Mausklick und läuft nach 15 s wieder click-through aus.
- Aktueller Release-Nachweis steht im Abschnitt „Artefakte und Git-Stand“.

Diese Änderungen sind noch keine visuelle Abnahme. Die verbleibenden Abschnitte
dieser Übergabe gelten weiter.

## Entscheidung

Der Native-Win32-Build ist **nicht abnahmefähig**. Er ist ein technisches Gerüst, kein
visueller oder funktionaler Ersatz für die Tauri-Version. Das frühere Release-Gate
`Build/CTest/2-s-Prozessstart` war nur ein technischer Smoke-Test und darf nicht als
GUI- oder Feature-Abnahme gewertet werden.

Die vom Nutzer bereitgestellten Vergleichsscreenshots (Native links, Tauri rechts)
belegen ungefähr 10–15 % sichtbare Parität; insgesamt ist eine Produktparität von
höchstens etwa 15–20 % vertretbar.

## Artefakte und Git-Stand

### Fortschritt 2026-09-22 (noch ohne visuelle Abnahme)

- Hauptansicht folgt nun dem Referenzaufbau: maximal 600 px breite, zentrierte,
  vertikal gestapelte Event-Karten statt drei Spalten.
- Eine geöffnete Karte schließt die anderen Karten; Footer wird unter dem
  tatsächlichen Kartenende angeordnet und kann Timer nicht mehr überdecken.
- Technische TTS-Untertitel wurden aus den Karten entfernt; Header nutzt das
  Zahnrad-Symbol wie Referenz. VS18-Release-Build, CTest 1/1 und 2-s-Prozessstart
  wurden nach dieser Änderung erneut bestanden.
- Settings haben nun echte Aktionen für Vorschau, 15-s-Positionieren und
  Positions-Reset (`d3265d0`). Rendering-Fehler des Overlays werden für D2D,
  DirectWrite, DC/DIB, BindDC, EndDraw und UpdateLayeredWindow protokolliert
  (`ffba24c`).
- Letzter portabler Release-Build nach Quellstand `ffba24c`: 455.168 B,
  SHA-256 `AE702162F0939A8C936BBCC60B16637BE5284C7A6EA2EB1634931C062EF1C251`.
  CTest 1/1 bestanden; PDB nicht in `dist`; Tauri-Artefakt unverändert.
- Offene Abnahme bleibt: echter Screenshot-/Overlay-/Audio-/DPI-Test. Ein
  Prozess-Smoke-Test ist kein visueller Beleg.

- Original: `dist\helltime.exe` (Tauri)
- Native: `dist\helltime-native.exe` (Win32, C++20/Direct2D/DirectWrite)
- Native-Artefakt beim letzten technischen Check: 455.168 B
- SHA-256: `AE702162F0939A8C936BBCC60B16637BE5284C7A6EA2EB1634931C062EF1C251`
- Letzte technische Checks: VS18-Release-Build, `CTest` 1/1, 2-s-Prozessstart
- Vorbestehend und absichtlich unberührt: untracked `AGENTS.md`, `mempalace.yaml`

## Bestätigte Hauptprobleme

### Hauptfenster

- Struktur der Karten ist korrigiert; Typografie, Rahmen, Abstände und Farben
  sind noch nicht screenshot-geprüft.
- Große ungenutzte schwarze Fläche; Höhe und Inhalt reagieren nicht wie im Original.
- Abweichende Typografie, Rahmen, Abstände, Farben, Titelbar und Bedienmuster.
- TTS-Namen, Footer-Überlappung und UTF-8-Fehler wurden korrigiert; echte
  Screenshot-Abnahme bleibt offen.

### Settings

Fehlen oder sind nicht gleichwertig: Hintergrundfarbe, Zeilen-Hintergrund-Deckkraft,
vollständige Sound-/Timer-Bedienung, TTS-Name, Audiotest, Debug-Status,
Scroll-/Responsive-Verhalten.
Mehrere Werte haben zu wenig Kontrast.

### Overlay

- **Release-Blocker.** Kein manueller Funktionsbeleg für sichtbares Ingame-Overlay.
- Toast bleibt ohne Reminder verborgen; Overview skaliert nach sichtbaren Zeilen.
- Native hat noch vereinfachtes Layout und feste Kategorie-Reihenfolge.
- Es fehlen Original-Look und -Verhalten: Typfarben, Gradients, Akzentleisten,
  Schatten und Boss-Unterzeile. Positioniermodus, 15-s-Rückkehr, Vorschau und
  Reset sind implementiert, aber nicht manuell belegt.
- Rendering-Fehler werden diagnostiziert, doch ein echter Sichtbarkeitstest bleibt
  nötig.

## Was tatsächlich vorhanden ist

- Lokale Schedule-Generierung für Helltide, Legion und World Boss.
- Basis-Reminder inkl. 1–3 Timer, Catch-up-Logik, SAPI-TTS und PCM-Beep.
- Native JSON-Settings unter `%LocalAppData%\HelltimeNative\settings.json`.
- Tray-Basis, Panic-Stop-Flag, Direct2D-Hauptfenster und Layered-Overlay-Fenster.

Das ist keine Aussage über vollständige Feature-Parität. Insbesondere fehlen ein
persistierter Fired-State, die Original-Reaktivierungslogik für Kategorien, der
Panic-Stop-Watchdog und die Tauri-Tray-Checkboxen.

## Verbleibende Abnahme

Nicht durchgeführt:

- Screenshot-Vergleich von Hauptfenster, Settings und Overlay gegen Tauri.
- Toast auslösen, 5,2-s-Lifecycle und Rückkehr zum Overview testen.
- Overlay sichtbar, click-through und Verschieben testen.
- 100/125/150 % DPI, Mehrmonitor, Diablo borderless und exklusives Fullscreen.
- Echte SAPI-TTS- und PCM-Beep-Ausgabe.
- Standby-/Wake-Catch-up im echten Prozess.

## Empfohlene Fortsetzung

1. Overlay-Lifecycle und sichtbares Rendering mit Fehlerprotokoll reparieren.
2. Screenshot-getrieben Main-/Settings-/Overlay-Layout nachbauen; zuerst Struktur,
   dann Typografie/Farben, zuletzt Mikrodetails.
3. Fehlende Bedienpfade implementieren: Preview, Positionieren, Reset, Farb- und
   Zeilen-Opacity, TTS-Name, Timer-Test, Tray-Toggles und Window-Persistenz.
4. Erst danach manuelle QA durchführen und Release-Größe/Hash neu prüfen.

## Arbeitsregeln

- Tauri-Variante bleibt unverändert und funktional, bis Native visuell und
  funktional abgenommen wurde.
- Keine World-Boss-Namen oder Ortsdaten erfinden; die lokale Schedule hat keine
  verifizierte Metadatenquelle.
- Nur task-eigene Dateien stagen; `AGENTS.md` und `mempalace.yaml` bleiben untracked.
