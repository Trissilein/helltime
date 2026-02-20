# Third-Party Notices

Last updated: 2026-02-20

This document lists important third-party services/frameworks used by helltime.
It is not legal advice and not a full transitive dependency inventory.
For full dependency inventories, see `package.json` and `src-tauri/Cargo.lock`.

## Special Thanks

A special and explicit thank-you to **helltides.com** for the event schedule data source:

- API endpoint used by this project: `https://helltides.com/api/schedule`
- helltime depends on this source for Helltide/Legion/World Boss timing data.

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
