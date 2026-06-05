# Large-File Governance

Large-file governance covers Redis route evidence, object-store delivery, delivered receipt reconciliation, S3/MinIO diagnostics, and read-only acceptance packages. The scripts operate on sanitized route logs and summaries; they do not require live Redis/S3 access unless a separate manual smoke explicitly starts those services.

## Object Routing Evidence

- Large files that exceed Redis Pub/Sub limits fall back to source-instance offline storage unless object routing is enabled.
- Object offers carry safe metadata such as `storeType`, `operation`, `reason`, `transferId`, object key, size, and hash evidence.
- E2E private large files route ciphertext objects and only expose envelope headers plus plaintext size/hash evidence needed by the receiver.

## Governance Scripts

- `scripts/analyze-large-file-route-logs.ps1` aggregates route results and rejects sensitive fields.
- `scripts/analyze-large-file-route-summary.ps1` turns route summaries into warning/failure buckets.
- `scripts/run-large-file-delivery-reconcile.ps1` compares delivered receipts with fallback summaries without changing queues or objects.
- `scripts/rotate-large-file-receipts.ps1` rotates sanitized delivered receipt JSONL files.
- `scripts/run-large-file-governance.ps1` orchestrates route analysis, S3 reason analysis, reconciliation, receipt rotation, alerts, dashboard, report, and diagnostics.
- `scripts/show-large-file-governance-status.ps1` emits a compact on-call status view and can fail on unhealthy governance state.

## Manual S3/MinIO Validation

`scripts/minio-s3-smoke.ps1` is the manual real-backend path. It verifies bucket/object PUT/HEAD/GET/DELETE behavior and may feed sanitized route/smoke summaries into acceptance packaging. Do not include endpoint credentials, access keys, secret keys, session tokens, Authorization, Credential, or Signature fields in committed artifacts.

