# Testing Coverage

This branch validates the QQNT backend through focused CTest targets, Rust bridge tests, protocol drift checks, and selected package/readme contract tests.

## Core Lanes

- `QQNTEngineSmoke` verifies headless engine startup, stdout NDJSON cleanliness, `ready`, and basic command acknowledgements.
- `QQNTEngineEndToEnd` verifies two real `QQNTEngine` processes can register, connect through `QQNTServer`, and deliver a private message over Redis routing.
- `QQNTProtocolDrift` verifies that docs, C++ fixtures, and Rust bridge expectations agree on protocol version and command/event coverage.
- `QQNTServerRedis`, `RedisServerReadiness`, and `RedisStartupRequirement` verify Redis-backed server readiness and routing behavior.
- `cargo test` under `tauri-qqnt/src-tauri` verifies Rust command wrappers, bridge parsing, event forwarding, and protocol-version guards.
- `npm run tauri build` verifies the Tauri bundle path and produces the local NSIS installer after the sidecars are built.
- Packaging and information-architecture tests verify that release packages include `README.md` and that documentation links remain discoverable.

## Current Scope

Backend validation should stay scoped to C++ sidecars, Redis/service behavior, Tauri Rust bridge code, protocol fixtures, and packaging scripts. Frontend rendering and mock UI behavior belong to `codex/qqnt-frontend`.
