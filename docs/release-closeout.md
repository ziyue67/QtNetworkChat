# Release Closeout

Release closeout for this repository is local and evidence-driven. The QQNT backend branch can produce sidecars, Rust bridge validation, package manifests, and documentation state, but environment-specific publishing remains an explicit step outside this repository.

## Closeout Checklist

- Build `QQNTEngine` and `QQNTServer` from the configured Qt/CMake build tree.
- Run focused backend CTest and Rust bridge tests before pushing backend commits.
- Keep `README.md` as the short entry point and keep detailed status in focused docs.
- Keep frontend-owned files on `codex/qqnt-frontend` so backend release evidence stays reviewable.

## Latest Backend Validation

2026-06-19 local validation on `codex/qqnt-backend`:

- `cmake --build build-qt6-mingw --target QQNTEngine QQNTServer` passed.
- Focused QQNT CTest lanes passed: `QQNTEngineSmoke`, `QQNTEngineEndToEnd`, `QQNTProtocolDrift`, Redis/server routing tests, group member update, file transfer, runtime hygiene, README information architecture, and Windows package manifest.
- `cargo fmt --check` and `cargo test` passed under `tauri-qqnt/src-tauri`.
- `npm run tauri build` passed and produced the local NSIS installer.

Current repository work is in closeout, not feature expansion. Any release wording must match verified local artifacts and recorded validation output.
