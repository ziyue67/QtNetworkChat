# Release Closeout

Release closeout is the final local evidence bundle for a current `HEAD`. It keeps code verification, local archive approval, install/diagnostic drill evidence, and environment-specific publication state separate so the repository can stop writing without pretending that an external release has already happened.

## Current Baseline

For the current branch baseline, local release closeout is already treated as complete when the generated manifests agree on all of the following:

- `local-release-review`: `reviewReady=true` and `reviewGate=review-complete-archive-decision-recorded`
- `release-archive-decision`: `decisionRecorded=true`, `decisionState=approved-local-archive`, and publication tracked separately as either `pending-environment-publication` or `published-outside-repo`
- `release-delivery-handoff`: `deliveryReady=true` and `deliveryGate=ready-local-delivery-handoff`
- `release-closeout-summary`: `closeoutReady=true` and `closeoutGate=release-closeout-ready-for-stop-writing`
- `release-final-local-archive`: `archiveReady=true` and `archiveGate=ready-final-local-archive`

When that combination is present, the repository is already in stop-writing mode for the verified current `HEAD`. Any later team-share upload, ticket handoff, release page update, or other environment-specific publishing step remains operational follow-up outside this repository; it does not reopen code verification or local archive readiness by itself.

## Artifacts

- `build-qt6-mingw/local-release-review/local-release-review.zip`
- `build-qt6-mingw/release-archive-decision/release-archive-decision.zip`
- `build-qt6-mingw/release-delivery-handoff/release-delivery-handoff.zip`
- `build-qt6-mingw/release-delivery-drill/release-delivery-drill-manifest.json`
- `build-qt6-mingw/release-publication-record.json`
- `build-qt6-mingw/release-closeout-summary/release-closeout-summary.zip`
- `build-qt6-mingw/release-final-local-archive/release-final-local-archive.zip`

## Intent

- `local-release-review` proves the current head passed the local verification and evidence gates needed before a final archive decision.
- `release-archive-decision` records whether the final local archive decision was still pending, approved, deferred, or rejected.
- `release-delivery-handoff` packages the operator-facing delivery bundle: Windows package metadata, upload plan, installer bootstrap, diagnostics collector, and handoff checklist.
- `release-delivery-drill` proves the packaged installer and sanitized diagnostics collector were exercised locally against the same release package.
- `release-publication-record` captures only the sanitized state of any environment-specific publication handoff; it never performs the external upload itself.
- `release-closeout-summary` is the single closeout overview that ties the above artifacts together and reports whether the local closeout chain is complete.
- `release-final-local-archive` is the stop-writing archive for the current head. It packages the closeout chain together with README, automation-status, and focused release docs so the repository can be left alone until an explicit publication-state update happens.

## Closeout Rules

- A local release closeout is considered complete only when the local release review, archive decision, delivery handoff, delivery drill, publication record, and diagnostics package are all present and readable.
- A final local archive is considered ready only when that closeout chain is complete and the current head has an explicit local archive decision recorded on the same baseline.
- Environment-specific publication remains outside the repository, even when the local closeout chain is complete.
- `pending-environment-publication` is a valid recorded follow-up state after local closeout is complete. It means the repository-side stop-writing archive is already ready, while publication is intentionally tracked as an external action.
- `published-outside-repo` is also a valid recorded follow-up state. It means the environment-specific publication handoff has been recorded without changing the underlying local code-closeout conclusion.
- `docs/automation-status.md` should summarize the stable gates and readiness state of this chain; it should not become the only place where release closeout semantics exist. Its recorded `HEAD` is the most recent verified evidence baseline, so the commit containing that file may be newer after the status document itself is committed.
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
  -ArchivePublishingStatus published-outside-repo `
  -ArchivePublishingChannel team-share
```

That refresh updates the closeout artifacts in a fixed order, regenerates the final local archive, and then rewrites `docs/automation-status.md` from the resulting manifests instead of relying on stale cross-references.
After the refresh, `docs/automation-status.md` is expected to report the current-head closeout baseline as the active release-review source whenever `local-release-review` and `release-closeout-summary` both point at the same reviewed HEAD.

## Remaining Follow-up

The current repository baseline is already closed out for repo scope. The remaining tail items are intentionally outside the code-closeout gate:

- `published-outside-repo`: the local archive is still the verified code-closeout baseline, and the environment-specific publication handoff has also been recorded.
- `pending-environment-publication`: the local archive is ready, but any team-share upload, ticket update, or release page record still has to be performed and recorded outside this repository.
- PostgreSQL release evidence is currently at `can-review-cutover`; if live evidence changes later, it may move back to operational review states such as `review-slow-queries` or `review-query-failures`. Treat the exact gate as a readback from current redacted artifacts rather than a fixed closeout label.
- Automation acknowledgement is currently `passing`; if future operational evidence fails again, the queue may return to `acknowledged-operational-remediation-gated` until remediation clears.
- Acknowledgement reminder is currently `not-required`; it may later move between `renew-soon`, `renew-required`, or `not-required` as live task history rolls forward. Treat it as operational freshness metadata, not a reopened code-closeout failure.

These items are useful to keep visible, but they do not invalidate the local release review, archive decision, closeout summary, or final local archive that already exist for the verified current-head baseline.

## Ongoing Maintenance

After repo-scope closeout is complete, the maintenance rule is simple:

- Keep `README.md`, `docs/automation-status.md`, `docs/release-closeout.md`, and `docs/e2e-hardening-status.md` synchronized whenever closeout artifacts, publication state, or status-script readback changes.
- Treat `docs/automation-status.md` as the rolling live-evidence surface for operational follow-up states, while the other docs keep the durable semantics and current-head closeout boundary.
- Update documentation and closeout artifacts in the same maintenance slice so the repository does not drift into mixed generations of current-state wording.
