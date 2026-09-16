# Third-Party Notices

Last updated: 2026-09-16

This document lists important third-party services/frameworks used by helltime.
It is not legal advice and not a full transitive dependency inventory.
For full dependency inventories, see `package.json` and `src-tauri/Cargo.lock`.

## Schedule Cadence Provenance

The local event generator uses cadence rules verified against the public **helltides.com** schedule on 2026-09-16:

- Helltide: each UTC hour, 55 minutes active.
- Legion: every 25 minutes from `2026-09-16T19:50:00Z`.
- World Boss: every 3.5 hours from `2026-09-16T20:00:00Z`.

helltime has no runtime dependency on this site. It opens `https://helltides.com/` only for the optional map action.

## Trademark / Affiliation Note

- `Diablo`, `Diablo IV`, `Vessel of Hatred`, and `Lord of Hatred` are trademarks of Blizzard Entertainment, Inc.
- helltime is an independent fan-made utility and is not affiliated with or endorsed by Blizzard Entertainment.

## Third-Party Frameworks and Libraries

### Tauri

- Project: <https://github.com/tauri-apps/tauri>
- License model: MIT or Apache-2.0
- Usage: desktop runtime/shell and native integration

### React / React DOM

- Projects:
  - <https://github.com/facebook/react>
  - <https://github.com/facebook/react/tree/main/packages/react-dom>
- License: MIT
- Usage: frontend UI rendering

### Vite

- Project: <https://github.com/vitejs/vite>
- License: MIT
- Usage: frontend build tooling

## Notes

- At this time, `src-tauri/tauri.conf.json` does not declare extra third-party binary resources beyond app assets/icons.
- If future releases bundle additional third-party binaries/services, update this file accordingly.
