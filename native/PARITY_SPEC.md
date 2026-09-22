# Helltime Native UI Parity Specification

## 1. Purpose and authority

This document defines visual and interaction parity for the Win32/D2D native
application. It is an implementation contract, not a redesign brief.

The authoritative product reference is the existing Tauri implementation:

- `src/App.tsx` — main window, settings modal, event-card interactions.
- `src/styles.css` — visual tokens, geometry, layers, states, and overlay CSS.
- `src/OverlayWindow.tsx` — overview/toast/positioning behavior and layout.
- `src/lib/overlay_window.ts` — overlay window lifecycle and positioning API.
- `src-tauri/icons/icon.ico` and `src-tauri/icons/icon.png` — application icon.

The native application must reproduce source behavior and visible structure.
Native-only controls, layout inventions, and visual substitutions are out of
scope unless this document is deliberately revised first.

### 1.1 Scope

Included: Windows native main window, settings modal, overlay, tray/window
icon, and source-equivalent controls.

Excluded: schedule-rule changes, Android, new product features, alternative
layouts, and changing the Tauri reference merely to accommodate native code.

### 1.2 Reference environment

Capture at 100% Windows display scale, dark title bar, no hover/focus state,
the same persisted settings, and the same injected test timestamp in both
applications. Dynamic content is masked as defined in section 8. A capture
made merely within the same real-time minute is not deterministic enough.

Provided reference screenshot measurements include outer Windows chrome. At
approximately 766 px outer width, Tauri content is 544 px wide; native content
currently is 600 px wide. Tauri closed cards are about 84 px tall; native cards
currently are 98 px tall. These measurements establish the visible target,
while CSS/D2D geometry below is the implementation source of truth.

## 2. Six reference states

Each state needs a Tauri baseline capture and a native capture with identical
data. Use these names for artifacts and automated screenshot cases.

| ID | State | Setup | Required visible result |
|---|---|---|---|
| R1 | Main, all enabled, closed | Overlay enabled; all event categories enabled; no expanded category | Header card, three compact cards sorted by due time, fixed Overlay/Position control, no footer panel |
| R2 | Main, one category expanded | Enable all; click Helltide header | Only Helltide expands; source timer/TTS/map controls appear; other cards close |
| R3 | Main, one category paused | Disable Legion by `Erinnern` | Legion is visually paused/disabled and sorted after enabled categories |
| R4 | Settings | Click header gear from R1 | Main stays behind dark blurred scrim; source settings sections/controls are visible; Debug remains collapsed |
| R5 | Overlay overview | `overlayWindowEnabled=true`, mode `overview`, all overlay/event categories enabled | Click-through, topmost three-row overview at its saved/default position; source row layout and adaptive size |
| R6 | Overlay toast and positioning | First click Vorschau in Toast mode; then click Positionieren | Toast appears for source preview duration. Positioning state has visible drag handle/border and becomes click-through after 15 s |

R6 is one reference-state family because it verifies both non-idle overlay
substates. Save two captures: `R6-toast` and `R6-positioning`.

## 3. Canonical tokens

Native rendering must use these values. Matching category colors alone is not
sufficient; source layering is part of visual parity.

### 3.1 Spacing, type, radius

| Token | Value |
|---|---:|
| `--space-1` through `--space-6` | 2, 4, 6, 8, 10, 12 px |
| `--text-xs`, `--text-sm`, `--text-base`, `--text-lg`, `--text-xl`, `--text-2xl` | 11, 12, 13, 14, 16, 18 px |
| weights regular/medium/semibold/bold/extrabold/black | 400/500/600/700/800/900 |
| `--radius-sm`, `--radius-md`, `--radius-lg`, `--radius-xl` | 4, 6, 8, 10 px |
| body font | `ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, Inter, Arial, sans-serif` |
| numeric/UI monospace | `ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, Liberation Mono, monospace` |

### 3.2 Colors

| Token | Value |
|---|---|
| `--bg`, `--bg-subtle` | `#0b0b0b`, `#121212` |
| `--gold`, `--gold-dim`, `--gold-bright` | `#d4af37`, `#a08428`, `#f4d03f` |
| `--burgundy`, `--burgundy-dark`, `--burgundy-bright` | `#0f0505`, `#050202`, `#1a0808` |
| text primary/secondary/tertiary/muted/disabled | white at 0.96/0.87/0.72/0.62/0.45 alpha |
| panel/panel-2 | white at 0.05/0.08 alpha |
| elevated 1/2/3 | white at 0.04/0.07/0.10 alpha |
| border subtle/default/strong | gray at 0.25/0.35/0.45 alpha |
| Helltide, Legion, World Boss | `#991f1f`, `#b32424`, `#7a1616` |
| bright Helltide, Legion, World Boss | `#b32424`, `#cc2929`, `#8f1a1a` |

### 3.3 Required layered effects

- Body: radial burgundy glow from top center, radial dark glow at bottom center,
  `#080808` to `#0a0a0a` vertical gradient, and fixed 2 px scanline overlay.
- Header/card/modal: gradients, border hierarchy, inset gold highlight, and
  source shadow/glow values from `src/styles.css`; no flat-color substitute.
- Category card: radial `--cat` glow from `10% 0%`; no solid 4 px left rail.
- Hover/focus effects may be implemented after base parity, but must use source
  colors, timing (`120/180/260 ms`), and not change resting geometry.

## 4. Main-window geometry and components

### 4.1 Window and container

- Source container: centered, `max-width: 560px`, `padding: 8px`.
- Main grid: 12 columns, `margin-top: 8px`, `gap: 6px`; every event card spans
  all 12 columns.
- Header: source card styling, `padding: 6px 8px`, 8 px radius, header gear in
  right action stack, `Lokale Zeitberechnung` below it.
- Do not draw a full-window gold strip. Do not draw a full-width footer card.
- Source floating overlay control: fixed `right: 8px`, `bottom: 8px`, flex gap
  6 px, padding `6px 8px`, 8 px radius, source gradient/blur/shadow.

### 4.2 Event-card structure

For each source category in `orderedTypes`:

1. A `categoryCard` uses its category token, source border/radial glow, and
   compact card header. It has no hard left accent rail.
2. Left action is a disclosure button. It displays title and optional World
   Boss subtitle, then `IN` or `ENDET` plus tabular countdown.
3. Right stack displays `AKTIV` or `PAUSIERT`, local event time, and the
   `Erinnern` checkbox.
4. Enabled categories sort ascending by timing target. Disabled categories are
   stable and always follow enabled categories.
5. Clicking a disabled card never expands it. Clicking an enabled header
   toggles that card and closes the other two.
6. Source disabled styling is opacity/filter/dashed state, not a different
   control arrangement.

### 4.3 Main visible controls

`Persisted path` uses the source `Settings` shape. A dash means transient UI
state only.

| Surface | Label / accessible name | Type | Value/range/step | Disabled rule | Persisted path | Action |
|---|---|---|---|---|---|---|
| Header | `Einstellungen` | icon button, gear | n/a | never | — | Open settings modal |
| Event card | event title/disclosure | button | `aria-expanded` boolean | disabled if category disabled | — | Toggle one expanded category |
| Event card | `AKTIV` / `PAUSIERT` | status output | category enabled state | n/a | `categories[type].enabled` | Display only |
| Event card | `Erinnern` | checkbox | boolean | never | `categories[type].enabled` | Enable/disable category |
| Floating | `Overlay` | checkbox | boolean | panic stop | `overlayWindowEnabled` | Show/hide overlay lifecycle |
| Floating | `Position` | button | n/a | panic stop or overlay disabled | position runtime/persisted bounds | Make overlay interactive for 15 s |

### 4.4 Expanded-card controls

| Condition | Label | Type | Value/range/step | Disabled rule | Persisted path | Action |
|---|---|---|---|---|---|---|
| Every enabled expanded card | `Timer Anzahl` | select | 1, 2, 3 | never | `categories[type].timerCount` | Changes number of timer editors |
| Every enabled expanded card | `TTS Name` | text input | source text; no HTML maxlength; normalizer limits persisted value to 80 characters | never | `categories[type].ttsName` | Changes spoken label |
| World Boss | `{boss}` hint | text output | n/a | n/a | — | Explains placeholder |
| Helltide | `Map öffnen` | primary small button | n/a | never | — | Opens `https://helltides.com/` |
| Helltide | map/mystery-chest hint | text output | n/a | n/a | — | Display only |
| Every configured timer | `Test` | button | n/a | visually enabled; handler exits on panic stop or muted sound | — | Plays timer beep, then optional TTS |
| Every timer | `Minuten vorher` / `Trigger` | range | 0..60, step 5 | never | `timers[index].minutesBefore` | Reminder lead time; zero means `now!` |
| Every timer | `TTS` | checkbox | boolean | never | `timers[index].ttsEnabled` | Enables TTS for that timer |
| Every timer | `Beep` | select | `beep`, `double`, `triple` | never | `timers[index].beepPattern` | Chooses beep pattern |
| Every timer | `Tonhöhe` | range | 200..2000 Hz, step 100 | never | `timers[index].pitchHz` | Chooses pitch |
| Every timer | `Ton testen` | icon button | n/a | disabled when global sound off | — | Plays selected beep |

Native must not replace source selects with cycle buttons, source discrete sliders
with continuous sliders, or source checkboxes with switch widgets.

## 5. Settings modal

### 5.1 Modal geometry and behavior

- Backdrop: fixed inset, black at 65% alpha, `backdrop-filter: blur(4px)`,
  top-aligned content, `padding: 10px 8px`, z-index 9999.
- Modal: width `min(680px, 100%)`; 10 px radius; source dark gradient;
  strong border/gold glow; source UI remains visible beneath backdrop.
- Header: `Einstellungen`, close icon button, 8 px/10 px padding, gold-tinted
  divider.
- Body: 10 px padding; `max-height: calc(100dvh - 120px)`; scrolls when needed.
- Escape and a backdrop click close the modal. Clicks inside do not close it.

### 5.2 Settings visible controls

| Section | Label | Type | Value/range/step | Disabled rule | Persisted path | Action |
|---|---|---|---|---|---|---|
| Overlay Verhalten | `Status` / `Overlay aktiv` or `Overlay aus` | status output | enabled state | n/a | `overlayWindowEnabled` | Display only |
| Overlay Verhalten | `Overview` | radio | one of overview/toast | panic stop or overlay disabled | `overlayWindowMode` | Overview mode |
| Overlay Verhalten | `Toast` | radio | one of overview/toast | panic stop or overlay disabled | `overlayWindowMode` | Toast mode |
| Overlay Verhalten | `Legion`, `Helltide`, `World Boss` | checkboxes | boolean | panic stop or overlay disabled | `overlayWindowCategories[type]` | Filter overview rows |
| Overlay Verhalten | `Vorschau` | button | n/a | panic stop or overlay disabled | — | Shows debug preview toast for 8 s |
| Overlay Verhalten | `Positionieren` | button | n/a | panic stop or overlay disabled | positioning deadline | Shows/focuses overlay; makes it interactive for 15 s |
| Overlay Look | `Hintergrundfarbe` | color input | `#rrggbb` | never in source | `overlayBgHex` | Changes overlay background color |
| Overlay Look | hex output | pill/output | current `#rrggbb` | n/a | `overlayBgHex` | Display only |
| Overlay Look | `Position zurücksetzen` | button | n/a | panic stop | overlay bounds | Recreates/resets overlay at `(40,40)` |
| Overlay Look | `Breite` | range | 60..200%, step 5 | never in source | `overlayScaleX` | Sets X scale |
| Overlay Look | `Höhe` | range | 60..200%, step 5 | never in source | `overlayScaleY` | Sets Y scale |
| Overlay Look | `Hintergrund-Transparenz` | range | 0..100, step 1; gamma-mapped opacity | never in source | `overlayBgOpacity` | Sets background opacity |
| Overlay Look | `Zeilen-Hintergrund` | range | 0..100, step 1; gamma-mapped opacity | never in source | `overlayLineBgOpacity` | Sets row opacity |
| Ton | `Benachrichtigungs-Ton`, `an`/`aus` | checkbox | boolean | panic stop | `soundEnabled` | Enables global audio |
| Ton | `Lautstärke` | range | 0..100%, step 1 | global sound disabled | `volume` | Sets volume; pointer-up/Enter/Space tests source beep |
| Event-spezifische Reminder | explanatory text | text output | n/a | n/a | — | Directs user to event cards |
| Footer | `Debug` | small button | expanded/collapsed | never | — | Toggles Debug section |
| Debug, when open | `Overlay Status` | button | n/a | panic stop | — | Shows window status and diagnostics |
| Debug, when open | `Logs leeren` | button | n/a | never | diagnostic storage | Clears diagnostics |
| Debug, when available | diagnostic output | preformatted text | last status/log lines | n/a | diagnostic storage | Display only |

### 5.3 Explicit removal list

Remove these native-visible inventions; no equivalent appears in `src/App.tsx`.

1. Main-window full-width footer with `Overlay` switch and `Einstellungen`.
2. Second settings entry in that footer. Header gear is the sole main settings
   entry.
3. Per-card unlabeled top-right switch.
4. Settings section `Erinnerungen` with `Auto` and `Toasts` checkboxes.
5. Timer-count button group `1/2/3`.
6. Cycle-only `Beep` button.
7. Any card side rail, full-window gold strip, or flat-card replacement that
   has no source counterpart.

`autoRefreshEnabled` and `systemToastsEnabled` may remain in persisted
compatibility data, but must not be presented as native controls unless the
Tauri reference first presents them too.

## 6. Overlay behavior and geometry

### 6.1 Lifecycle contract

| Mode/state | Required result |
|---|---|
| Overlay disabled or panic stop | Overlay window hidden |
| Overview, at least one enabled row | Window visible immediately, topmost, click-through, on current workspace |
| Overview, no enabled rows | Window hidden except during positioning |
| Toast, idle | Window hidden. This is intentional, not a failed mode change. |
| Toast preview | Show source preview for 8.0 s ±0.5 s |
| Due reminder | Show source event toast for 5.2 s ±0.5 s |
| Positionieren | Show/focus overlay, accept pointer events for 15 s, then restore click-through |
| Reset position | Reset to `(40,40)`, size baseline, visible enough to recover |

The native application currently has all visible setting gates true in
`%LOCALAPPDATA%\\HelltimeNative\\settings.json` and no persisted position file.
An invisible Overview is therefore not explained by disabled settings or an
off-screen saved position. Creation, render, and show failures need observable
native diagnostics before claiming a root cause.

### 6.2 Overlay visual geometry

- Source starts from base width 304 px; width follows X scale and cannot fall
  below 170 px.
- Source height starts from 132 px, follows Y scale, and grows to avoid content
  clipping; it never falls below 56 px.
- Source host padding: `5px * scale` vertical, `7px * scale` horizontal; 10 px
  radius.
- Overview rows: vertical flex, 4 px scaled gap, two columns
  `minmax(0,1fr) auto`, 5/7 px scaled padding, 8 px scaled radius, 1 px white
  8% border, black vertical row gradient, left category-color marker.
- Overview text: event 14 px/900; optional World Boss subtitle 11.5 px/800;
  time 18 px/950 tabular. Event time is not a third source row.
- Toast line: same two-column hierarchy, 6/8 px scaled padding, 10 px scaled
  radius, source shadow. It contains title and countdown only.
- Positioning: outline, black/red shadow, pulse animation, and draggable
  `Ziehen zum Verschieben` handle. Background is inverted and opaque during
  positioning.

### 6.3 Native observability requirement

Expose source-equivalent Debug status for every overlay operation. It must show
creation success, HWND/window existence, visibility, bounds, mode, gate values,
last HRESULT/Win32 error, and last `UpdateLayeredWindow` result. `Create()` may
not fail silently; callers must handle a false result. `OutputDebugString`
alone is insufficient user-facing diagnosis.

## 7. Native icon contract

Reuse existing `src-tauri/icons/icon.ico`; do not create a new icon design.

- Compile it into native executable through a Windows resource file.
- Set main window `hIcon` and `hIconSm` on class/window creation.
- Set `WM_SETICON` large and small handles if required by class setup.
- Use same icon for `NOTIFYICONDATA::hIcon` rather than `IDI_APPLICATION`.
- Verify title bar, Alt-Tab/taskbar, and tray use Helltime icon.

## 8. Screenshot comparison and masks

### 8.1 Capture process

1. Reset both applications to comparable settings and display scale.
2. Capture Tauri first, then native, each state in section 2.
3. Capture whole window including client area; record outer size, client size,
   monitor DPI, and timestamp in sidecar JSON.
4. Derive structural anchors from source capture: container, header, each card,
   floating control, modal, and overlay host rectangles.
5. Compare native against source after applying only approved masks.

### 8.2 Approved masks

Mask only nondeterministic content:

- OS title bar controls, shadow, and resize border.
- Countdown text (`.panelHeaderMeta`, `.overlayLineTime`, `.overlayToastTime`).
- Local event time (`.categoryTimePill`) and dynamic `IN`/`ENDET` content when
  timing crosses a state boundary.
- World Boss dynamic subtitle/location.
- Scrollbar thumb/track when platform layout changes it.
- Hover, focus-ring, caret, animated pulse phase, and antialiased glyph pixels
  inside the dynamic text rectangles above.

Never mask container/card/modal/overlay geometry, borders, gradients,
backgrounds, icons, static labels, checkbox/radio placement, or buttons.

### 8.3 Acceptance thresholds

| Check | Pass condition |
|---|---|
| Structural anchors | Container/header/card/modal/floating-control edges within 2 px; gaps within 1 px |
| R1 closed card | 84 px ±2 at reference capture; three source-order cards; no footer panel |
| Source container | 560 px max content model; at provided reference width visible content 544 px ±2 |
| Static color | Unmasked sampled RGB channels differ by at most 8, after identical capture compositing |
| Static pixel area | At least 95% of unmasked static pixels within per-channel delta 16 |
| Controls | Every section 4/5 visible control exists once with exact label/type/range/step/disabled/persistence/action |
| Removal list | Zero native-only listed controls/elements visible |
| R5 overview | Default three-row overview visible within 1 s, correct source row geometry, click-through |
| R6 toast | Preview remains visible 8.0 s ±0.5 s; due toast 5.2 s ±0.5 s |
| R6 positioning | Drag works during 15 s window; then returns click-through |
| Icon | Main title bar, taskbar/Alt-Tab, and tray contain Helltime icon, never generic application icon |

## 9. Functional regression matrix

Automate or manually execute every row before parity is complete.

| Case | Expected result |
|---|---|
| Enable/disable each category | Correct persistence, visual paused state, disabled category sorted last |
| Expand each category | Exactly one enabled category open; source controls shown |
| Timer lead value 0 | Persisted and displayed as source `Trigger: now!` |
| Timer lead values | Only 5-minute values selectable |
| Pitch values | Only 100-Hz values from 200 to 2000 selectable |
| Sound disabled | Volume/tone-test disabled exactly where source disables them |
| Overview filters | Each overlay category checkbox affects only its row |
| Toast idle | No idle toast window/content |
| Preview | Correct visible preview duration and geometry |
| Reset position | Recoverable `(40,40)` position |
| Panic stop | Overlay/audio actions blocked; source warning/reset behavior present |
| Restart | All persisted source paths retain values; transient expanded/debug states do not require persistence |

## 10. Delivery order

1. Overlay diagnostics and lifecycle proof. Do not call overlay fixed before R5
   and R6 visual proof exists.
2. Main-window structural parity and icon.
3. Card/modal control parity and removal list.
4. Gradient/token/overlay rendering parity.
5. Screenshot and functional regression proof.

No phase is complete from a successful compile alone. Completion requires the
relevant reference capture, masks, pixel/anchor result, and interactive proof.
