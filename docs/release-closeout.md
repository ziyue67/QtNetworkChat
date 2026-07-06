# Release Closeout

Release closeout for this repository is local and evidence-driven. The QtNetworkChat backend branch can produce QtNetworkChat, QtNetworkChat validation, package manifests, and documentation state, but environment-specific publishing remains an explicit step outside this repository.

## Closeout Checklist

- Build `QtNetworkChatEngine` and `QtNetworkChatServer` from the configured Qt/CMake build tree.
- Run focused backend CTest and QtNetworkChat tests before pushing backend commits.
- Keep `README.md` as the short entry point and keep detailed status in focused docs.
- Keep frontend-owned files on `codex/QtNetworkChat-frontend` so backend release evidence stays reviewable.

## Latest Backend Validation

2026-06-19 local validation on `codex/QtNetworkChat-backend`:

- `cmake --build build-qt6-mingw --target QtNetworkChatEngine QtNetworkChatServer` passed.
- CTest passed for the full 97-test suite when run in numbered slices: `-I 1,24`, `-I 25,54`, `-I 55,79`, and `-I 80,97`.
- `cargo fmt --check` and `cargo test` passed under `QtNetworkChat-QtNetworkChat/src-QtNetworkChat`.
- `npm run QtNetworkChat build` passed and produced the local NSIS installer.

Current repository work is in closeout, not feature expansion. Any release wording must match verified local artifacts and recorded validation output.
