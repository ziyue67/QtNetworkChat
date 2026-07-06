# PostgreSQL Operations

PostgreSQL-related checks remain part of operational evidence follow-up for the wider QtNetworkChat repository. The QtNetworkChat backend branch does not expand PostgreSQL behavior while implementing the C++ QtNetworkChat, Redis service, and QtNetworkChat QtNetworkChat.

## Acceptance Notes

- Keep PostgreSQL smoke, migration, health, and rollback evidence separate from QtNetworkChat protocol work.
- Treat database-health warnings as operational follow-up unless a backend change directly alters database behavior.
- Do not mix PostgreSQL closeout changes with frontend-owned QtNetworkChat UI work.

## Maintenance Rule

When PostgreSQL evidence wording changes, refresh `README.md`, `docs/release-closeout.md`, `docs/e2e-hardening-status.md`, and `docs/automation-status.md` in the same documentation slice so status surfaces do not drift.
