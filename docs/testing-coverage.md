# Testing Coverage

This branch validates the QQNT backend through CTest, Rust bridge tests, protocol drift checks, and selected package/readme contract tests.

## Core Lanes

- `QQNTEngineSmoke` verifies headless engine startup, stdout NDJSON cleanliness, `ready`, basic command acknowledgements, every documented command in `tests/fixtures/protocol_contract.json` is routed by `QQNTEngine`, and successful contract acknowledgements keep their documented payload shape.
- `QQNTEngineEndToEnd` verifies two real `QQNTEngine` processes can register, connect through `QQNTServer`, and deliver a private message over Redis routing.
- `QQNTProtocolDrift` verifies that docs, C++ fixtures, C++ router helpers, and Rust bridge expectations agree on protocol version, command/event coverage, command/event payload shapes, strict command envelope/payload field validation, and the `details.targetFields` contract for target-validation errors.
- `QQNTServerRedis`, `RedisServerReadiness`, and `RedisStartupRequirement` verify Redis-backed server readiness and routing behavior.
- `cargo test` under `tauri-qqnt/src-tauri` verifies Rust command wrappers, bridge parsing, event forwarding, and protocol-version guards.
- `npm run tauri build` verifies the Tauri bundle path and produces the local NSIS installer after the sidecars are built.
- Packaging and information-architecture tests verify that release packages include `README.md` and that documentation links remain discoverable; `ReadmeInformationArchitecture` also keeps the backend quick-start commands, B14/BM6 Tauri packaging contract, and backend/frontend branch ownership wording aligned with `package.json`, `tauri.conf.json`, and the branch plan.
- `RuntimeArtifactHygiene` verifies that Phase 8.4 runtime databases, histories, offline attachment payloads, Tauri generated output, sidecar executables, and local package/build artifacts remain ignored and untracked.

## Current Scope

Backend validation should stay scoped to C++ sidecars, Redis/service behavior, Tauri Rust bridge code, protocol fixtures, and packaging scripts. Frontend rendering and mock UI behavior belong to `codex/qqnt-frontend`.

On 2026-06-19, the backend branch completed the 97-test CTest suite in numbered slices (`-I 1,24`, `-I 25,54`, `-I 55,79`, `-I 80,97`). Slicing avoids the local 5-minute command timeout while preserving coverage of every configured CTest entry.

Running `npm test -- --run` in the backend branch is not a BM5/BM6 gate because the current Vitest suite is owned by `codex/qqnt-frontend`; with no `*.test.ts(x)` files present, Vitest exits with `No test files found` and should be interpreted as out of backend scope rather than a backend regression.
