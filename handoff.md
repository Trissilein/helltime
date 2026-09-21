# Helltime – Native Win32 Handoff

Stand: 2026-09-22  
Repository: `D:\GIT\helltime`  
Basis vor diesem Handoff: `13b490c feat: add native Win32 Helltime client`

## Entscheidung

Der Native-Win32-Build ist **nicht abnahmefähig**. Er ist ein technisches Gerüst, kein
visueller oder funktionaler Ersatz für die Tauri-Version. Das frühere Release-Gate
`Build/CTest/2-s-Prozessstart` war nur ein technischer Smoke-Test und darf nicht als
GUI- oder Feature-Abnahme gewertet werden.

Die vom Nutzer bereitgestellten Vergleichsscreenshots (Native links, Tauri rechts)
belegen ungefähr 10–15 % sichtbare Parität; insgesamt ist eine Produktparität von
höchstens etwa 15–20 % vertretbar.

## Artefakte und Git-Stand

- Original: `dist\helltime.exe` (Tauri)
- Native: `dist\helltime-native.exe` (Win32, C++20/Direct2D/DirectWrite)
- Native-Artefakt beim letzten technischen Check: 446.976 B
- SHA-256: `BB91D2A6A1610BC37C32F4A4C04A3A98670BA29EB2E10E4DFD7D9EDF69AAF50A`
- Letzte technische Checks: VS18-Release-Build, `CTest` 1/1, 2-s-Prozessstart
- Vorbestehend und absichtlich unberührt: untracked `AGENTS.md`, `mempalace.yaml`

## Bestätigte Hauptprobleme

### Hauptfenster

- Falsches Grundlayout: drei breite horizontale Karten statt der gestapelten,
  zentrierten Tauri-Karten.
- Große ungenutzte schwarze Fläche; Höhe und Inhalt reagieren nicht wie im Original.
- Abweichende Typografie, Rahmen, Abstände, Farben, Titelbar und Bedienmuster.
- Interne TTS-Namen erscheinen fälschlich in den Karten; `{boss}` wird sichtbar.
- Aufgeklappte Karten wachsen nicht sauber im Layout: der Overlay-Footer überdeckt
  Timer-Inhalt und Bedienelemente.
- Encoding-Fehler sichtbar: `Höhe` erscheint als `HÃ¶he`.

### Settings

Fehlen oder sind nicht gleichwertig: Overlay-Status, Vorschau, Positionieren,
Positions-Reset, Hintergrundfarbe, Zeilen-Hintergrund-Deckkraft, vollständige
Sound-/Timer-Bedienung, TTS-Name, Audiotest, Debug-Status, Scroll-/Responsive-Verhalten.
Mehrere Werte haben zu wenig Kontrast.

### Overlay

- **Release-Blocker.** Kein manueller Funktionsbeleg für sichtbares Ingame-Overlay.
- Native erzwingt im Tick `SWP_SHOWWINDOW`; Toast-Modus bleibt daher nicht bis zum
  Reminder verborgen. Siehe `native/src/integration/overlay.cpp`.
- Ohne Reminder rendert Toast-Modus eine Kategorie statt leer zu bleiben.
- Native hat starre Größe und feste Kategorie-Reihenfolge.
- Es fehlen Original-Look und -Verhalten: Typfarben, Gradients, Akzentleisten,
  Schatten, Boss-Unterzeile, sichtbarer Positioniermodus, 15-s-Rückkehr zu
  click-through, Vorschau und Reset.
- `UpdateLayeredWindow`, Direct2D- und DC-Fehler werden nicht geprüft oder geloggt;
  damit bleibt ein unsichtbares Overlay nicht diagnostizierbar.

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
