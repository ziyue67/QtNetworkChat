# Release Closeout

Release closeout for this repository is local and evidence-driven. The QQNT backend branch can produce sidecars, Rust bridge validation, package manifests, and documentation state, but environment-specific publishing remains an explicit step outside this repository.

## Closeout Checklist

- Build `QQNTEngine` and `QQNTServer` from the configured Qt/CMake build tree.
- Run focused backend CTest and Rust bridge tests before pushing backend commits.
- Keep `README.md` as the short entry point and keep detailed status in focused docs.
- Keep frontend-owned files on `codex/qqnt-frontend` so backend release evidence stays reviewable.

Current repository work is in closeout, not feature expansion. Any release wording must match verified local artifacts and recorded validation output.
