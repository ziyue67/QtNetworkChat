# QQ NT Tauri Frontend

QQ NT desktop client frontend built with Tauri v2, React, TypeScript, Vite, Tailwind CSS, Zustand, and Vitest.

## Scope

This directory owns the desktop client UI only:

- frameless Tauri client shell and React routing
- login/register UI with a 300x460 close-only window
- main QQ NT layout with sidebar navigation and window controls
- Zustand stores for auth, contacts, sessions, messages, files, and UI settings
- IPC command wrappers and engine event listeners for the Rust/C++ backend bridge
- mock placeholders for QQ NT extension entries that do not yet have real backend logic

Backend sidecars, Rust Tauri bridge code, Redis, and C++ engine/server work are owned by the backend branch.

## Scripts

```bash
npm install
npm run test -- --run
npm run build
npm run tauri dev
```

## Window Behavior

- Login/register starts at `300x460`, disables resizing after React mounts, and exposes only the close button.
- The login/register title bar is draggable and the form does not show Redis, IP address, port, or server fields.
- After login, the main window resizes to the QQ NT shell and shows minimize, maximize/restore, and close controls.

## Frontend Notes

- Network host/port settings live under the in-app Settings page after login.
- When the local engine is unavailable, the UI enters temporary Mock mode so frontend flows can still be reviewed.
- Run `npm run build` before handoff to catch TypeScript and production bundle errors.
