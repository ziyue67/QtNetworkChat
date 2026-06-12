# Release Closeout

Release closeout is the final local evidence bundle for a current `HEAD`. It keeps code verification, local archive approval, install/diagnostic drill evidence, and environment-specific publication state separate so the repository can stop writing without pretending that an external release has already happened.

## Artifacts

- `build-qt6-mingw/local-release-review/local-release-review.zip`
- `build-qt6-mingw/release-archive-decision/release-archive-decision.zip`
- `build-qt6-mingw/release-delivery-handoff/release-delivery-handoff.zip`
- `build-qt6-mingw/release-delivery-drill/release-delivery-drill-manifest.json`
- `build-qt6-mingw/release-publication-record.json`
- `build-qt6-mingw/release-closeout-summary/release-closeout-summary.zip`

## Intent

- `local-release-review` proves the current head passed the local verification and evidence gates needed before a final archive decision.
- `release-archive-decision` records whether the final local archive decision was still pending, approved, deferred, or rejected.
- `release-delivery-handoff` packages the operator-facing delivery bundle: Windows package metadata, upload plan, installer bootstrap, diagnostics collector, and handoff checklist.
- `release-delivery-drill` proves the packaged installer and sanitized diagnostics collector were exercised locally against the same release package.
- `release-publication-record` captures only the sanitized state of any environment-specific publication handoff; it never performs the external upload itself.
- `release-closeout-summary` is the single closeout overview that ties the above artifacts together and reports whether the local closeout chain is complete.

## Closeout Rules

- A local release closeout is considered complete only when the local release review, archive decision, delivery handoff, delivery drill, publication record, and diagnostics package are all present and readable.
- Environment-specific publication remains outside the repository, even when the local closeout chain is complete.
- `docs/automation-status.md` should summarize the stable gates and readiness state of this chain; it should not become the only place where release closeout semantics exist.
- When the default fail-closed E2E release evidence artifact and the current-head closeout chain disagree, the active release-review baseline is the current-head `local-release-review` plus `release-closeout-summary` pair. The older baseline remains useful as an informational artifact, but it must not override a current-head closeout that is already complete.

## Refresh

Use the closeout refresh script to keep every artifact on the same baseline:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/refresh-release-closeout.ps1 `
  -BuildDir build-qt6-mingw `
  -RunDeliveryDrill `
  -ArchiveDecisionState approved-local-archive `
  -ArchiveDecidedBy jun23 `
  -ArchiveDecisionReason "Current-head release closeout verified locally." `
  -ArchivePublishingStatus pending-environment-publication `
  -ArchivePublishingChannel team-share
```

That refresh updates the closeout artifacts in a fixed order and regenerates `docs/automation-status.md` from the resulting manifests instead of relying on stale cross-references.
After the refresh, `docs/automation-status.md` is expected to report the current-head closeout baseline as the active release-review source whenever `local-release-review` and `release-closeout-summary` both point at the same reviewed HEAD.
