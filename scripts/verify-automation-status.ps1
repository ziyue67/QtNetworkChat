param(
    [Parameter(Mandatory = $true)]
    [string]$ScriptPath
)

$ErrorActionPreference = "Stop"

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [Parameter(Mandatory = $true)]
        [string]$Expected
    )
    if (-not $Text.Contains($Expected)) {
        throw "Automation status missing expected text: $Expected || markdown=$Text"
    }
}

function Assert-NotContains {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [Parameter(Mandatory = $true)]
        [string]$Forbidden
    )
    if ($Text.Contains($Forbidden)) {
        throw "Automation status leaked forbidden text: $Forbidden"
    }
}

function Assert-DoesNotMatch {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [Parameter(Mandatory = $true)]
        [string]$Pattern
    )
    if ($Text -match $Pattern) {
        throw "Automation status matched forbidden pattern: $Pattern"
    }
}

function Assert-NoFixedMirrorBranchPolicy {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text
    )
    foreach ($pattern in @(
        'origin/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',
        'fast-forward\s+[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',
        'push\s+main,\s+then\s+fast-forward\s+[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+'
    )) {
        Assert-DoesNotMatch -Text $Text -Pattern $pattern
    }
}

function Ensure-Directory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

$sampleRunId = "{0}-{1}" -f $PID, ([guid]::NewGuid().ToString("N"))
$tempDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ("..\automation_status_sample_{0}" -f $sampleRunId)))
$configuredTempDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ("..\automation_status_configured_missing_sample_{0}" -f $sampleRunId)))
Remove-Item -Recurse -Force $tempDir, $configuredTempDir -ErrorAction SilentlyContinue
Ensure-Directory -Path $tempDir
Ensure-Directory -Path $configuredTempDir

$markdownPath = Join-Path $tempDir "automation-status.md"
$dbStatusPath = Join-Path $tempDir "database-health-status.json"
$dbLastRunPath = Join-Path $tempDir "database-health-last-run.log"
$dbPreviewPath = Join-Path $tempDir "database-health-task-preview.json"
$govStatusPath = Join-Path $tempDir "large-file-governance-status.json"
$govLastRunPath = Join-Path $tempDir "large-file-governance-last-run.log"
$govPreviewPath = Join-Path $tempDir "large-file-governance-task-preview.json"
$taskHistoryPath = Join-Path $tempDir "automation-task-history.json"
$taskAckPath = Join-Path $tempDir "automation-task-ack.json"
$autoRunListPath = Join-Path $tempDir "gh-run-list.json"
$releaseCiStatusPath = Join-Path $tempDir "github-windows-build-status-release.json"
$autoCTestLogPath = Join-Path $tempDir "LastTest.log"
$localVerificationPath = Join-Path $tempDir "local-verification-status.json"
$e2eRolloutDir = Join-Path $tempDir "e2e_rollout_observability_evidence"
$e2eRolloutJsonPath = Join-Path $e2eRolloutDir "e2e-rollout-observability.json"
$e2eRolloutMarkdownPath = Join-Path $e2eRolloutDir "e2e-rollout-observability.md"
$e2eReleaseEvidenceDir = Join-Path $tempDir "e2e_release_evidence"
$e2eReleaseEvidenceManifestPath = Join-Path $e2eReleaseEvidenceDir "e2e-release-evidence-manifest.json"
$bootstrapScriptPath = Join-Path $PSScriptRoot "bootstrap-automation-tasks.ps1"
Ensure-Directory -Path $e2eRolloutDir
Ensure-Directory -Path $e2eReleaseEvidenceDir

@'
{
  "format":"qtnetworkchat-database-health-status-v1",
  "status":"healthy",
  "ok":true,
  "driver":"QPSQL",
  "checkCount":4,
  "failedChecks":[],
  "queryMetrics":{"slowQueryCount":2,"queryFailureCount":1},
  "summary":{"readiness":"verified","operatorAction":"Investigate query failures before promoting this database health snapshot."},
  "auditSummary":{"releaseGate":"review-query-failures","auditFocus":["query-failures","slow-queries"]}
}
'@ | Set-Content -LiteralPath $dbStatusPath -Encoding UTF8
'2026-06-03T01:02:03.0000000Z healthExitCode=0 statusExitCode=0 dashboardExitCode=0 exitCode=0 healthPath=redacted statusPath=redacted dashboardPath=redacted markdownPath=redacted' |
    Set-Content -LiteralPath $dbLastRunPath -Encoding UTF8
@'
{
  "format":"qtnetworkchat-large-file-governance-status-v1",
  "status":"unhealthy",
  "ok":false,
  "totalWarnings":3,
  "alertCount":2,
  "s3CoverageActionableGapAreas":["remote-validation-fail-closed"]
}
'@ | Set-Content -LiteralPath $govStatusPath -Encoding UTF8
'2026-06-03T02:03:04.0000000Z exitCode=2' | Set-Content -LiteralPath $govLastRunPath -Encoding UTF8
@'
{
  "format":"qtnetworkchat-automation-task-history-v1",
  "runCount":3,
  "failedRunCount":1,
  "latestRun":{"timestamp":"2026-06-03T03:02:03.0000000Z","exitCode":0},
  "acknowledged":true,
  "ackExpired":false,
  "ackExpiryHours":72,
  "ackAgeHours":1.5,
  "ackExpiresAt":"2026-06-06T03:30:00.0000000Z",
  "ackHoursRemaining":70.5,
  "ackHoursOverdue":0,
  "ackReminder":"acknowledged"
}
'@ | Set-Content -LiteralPath $taskHistoryPath -Encoding UTF8
@'
{
  "acknowledged":true,
  "acknowledgedBy":"oncall-user",
  "acknowledgedAt":"2026-06-03T03:30:00.0000000Z",
  "reason":"reviewed"
}
'@ | Set-Content -LiteralPath $taskAckPath -Encoding UTF8

@'
[
  {
    "databaseId": 99112233,
    "headSha": "auto1234567890abcdef",
    "status": "completed",
    "conclusion": "success",
    "createdAt": "2026-06-03T04:00:00Z",
    "displayTitle": "feat: auto status",
    "workflowName": "Windows Build"
  }
]
'@ | Set-Content -LiteralPath $autoRunListPath -Encoding UTF8
& (Join-Path $PSScriptRoot "write-github-windows-build-status.ps1") `
    -OutputPath $releaseCiStatusPath `
    -Head "missing-release-head" `
    -RunListJsonPath $autoRunListPath `
    -FailOnSensitive | Out-Null
@'
Start testing: Jun 03 04:00
----------------------------------------------------------
1/2 Testing: One
Test Passed.
2/2 Testing: Two
Test Passed.
End testing: Jun 03 04:01
'@ | Set-Content -LiteralPath $autoCTestLogPath -Encoding UTF8

& (Join-Path $PSScriptRoot "write-local-verification-status.ps1") `
    -OutputPath $localVerificationPath `
    -BuildStatus passed `
    -BuildExitCode 0 `
    -CTestStatus passed `
    -CTestExitCode 0 `
    -CTestCount 2 `
    -CTestLogPath $autoCTestLogPath `
    -FailOnSensitive | Out-Null

@'
{
  "format":"qtnetworkchat-e2e-production-rollout-observability-evidence-v1",
  "status":"blocked",
  "ok":false,
  "summary":{
    "readiness":"blocked",
    "filesystemObjectRecoveryReady":true,
    "filesystemObjectRecoveryReleaseGate":"e2e-filesystem-object-ciphertext-readback-ready",
    "offlineObjectRecoveryReady":true,
    "offlineObjectRecoveryScope":"offline-ciphertext-readback",
    "offlineObjectRecoveryReleaseGate":"e2e-offline-ciphertext-readback-reviewed-opt-in"
  },
  "auditSummary":{
    "releaseGate":"production-rollout-observability-blocked-not-linked",
    "auditFocus":["production-crypto-acceptance"],
    "evidenceBundle":["e2e-rollout-observability.json","e2e-rollout-observability.md"]
  },
  "releaseRun":{
    "tool":"e2e_rollout_observability_exporter",
    "persisted":true
  },
  "sensitiveExportProof":{
    "noSensitiveExportProof":false,
    "sensitiveFieldsSuppressed":true,
    "rawKeyExported":false,
    "privateMaterialExported":false,
    "sessionSecretExported":false,
    "plaintextBytesExported":false,
    "ciphertextBytesExported":false
  }
}
'@ | Set-Content -LiteralPath $e2eRolloutJsonPath -Encoding UTF8
@'
# QtNetworkChat E2E Production Rollout Observability Evidence

- Status: `blocked`
- Release gate: `production-rollout-observability-blocked-not-linked`
- Offline/object recovery gate: `e2e-offline-ciphertext-readback-reviewed-opt-in`
'@ | Set-Content -LiteralPath $e2eRolloutMarkdownPath -Encoding UTF8
& (Join-Path $PSScriptRoot "package-e2e-release-evidence.ps1") `
    -OutputDir $e2eReleaseEvidenceDir `
    -RolloutJsonPath $e2eRolloutJsonPath `
    -RolloutMarkdownPath $e2eRolloutMarkdownPath `
    -GitHubWindowsBuildStatusPath $releaseCiStatusPath `
    -LocalVerificationStatusPath $localVerificationPath `
    -FailOnSensitive | Out-Null

([ordered]@{
    format = "qtnetworkchat-database-health-task-preview-v1"
    taskKind = "database-health"
    taskDisplayName = "Database health"
    statusArtifactPath = $dbStatusPath
    lastRunPath = $dbLastRunPath
    historyArtifactPath = $taskHistoryPath
    ackArtifactPath = $taskAckPath
    taskSummary = "Read-only database health check that writes redacted health, status, optional dashboard, last-run, and task history artifacts."
    readOnly = $true
    register = $false
    schedule = "Daily"
    at = "03:15"
    artifactRoles = [ordered]@{
        status = "statusArtifactPath"
        lastRun = "lastRunPath"
        history = "historyArtifactPath"
        ack = "ackArtifactPath"
    }
} | ConvertTo-Json) | Set-Content -LiteralPath $dbPreviewPath -Encoding UTF8

([ordered]@{
    format = "qtnetworkchat-large-file-governance-task-preview-v1"
    taskKind = "large-file-governance"
    taskDisplayName = "Large-file governance"
    statusArtifactPath = $govStatusPath
    lastRunPath = $govLastRunPath
    historyArtifactPath = $taskHistoryPath
    ackArtifactPath = $taskAckPath
    taskSummary = "Read-only governance sweep that writes redacted dashboard, reports, diagnostics, last-run, and task history artifacts."
    readOnly = $true
    register = $false
    schedule = "Hourly"
    at = "02:30"
    everyHours = 2
    artifactRoles = [ordered]@{
        status = "statusArtifactPath"
        lastRun = "lastRunPath"
        history = "historyArtifactPath"
        ack = "ackArtifactPath"
    }
    launcherPath = (Join-Path $tempDir "task\run-large-file-governance-task.ps1")
} | ConvertTo-Json) | Set-Content -LiteralPath $govPreviewPath -Encoding UTF8

& $ScriptPath `
    -MarkdownPath $markdownPath `
    -Head "abc1234" `
    -OriginMain "abc1234" `
    -CiStatus "success" `
    -CiRunId "26816554264" `
    -BuildStatus "passed" `
    -CTestStatus "passed" `
    -CTestCount 51 `
    -E2ERolloutObservabilityJsonPath $e2eRolloutJsonPath `
    -E2ERolloutObservabilityMarkdownPath $e2eRolloutMarkdownPath `
    -E2EReleaseEvidenceManifestPath $e2eReleaseEvidenceManifestPath `
    -DatabaseHealthStatusPath $dbStatusPath `
    -DatabaseHealthLastRunPath $dbLastRunPath `
    -DatabaseHealthTaskPreviewPath $dbPreviewPath `
    -LargeFileGovernanceStatusPath $govStatusPath `
    -LargeFileGovernanceLastRunPath $govLastRunPath `
    -LargeFileGovernanceTaskPreviewPath $govPreviewPath `
    -AutomationTaskHistoryPath $taskHistoryPath `
    -AutomationTaskAckPath $taskAckPath `
    -ProtectedUntracked ".polaris/,AGENTS.md" `
    -FailOnSensitive

if (-not (Test-Path -LiteralPath $markdownPath -PathType Leaf)) {
    throw "Automation status Markdown was not created"
}

$markdown = Get-Content -LiteralPath $markdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'QtNetworkChat Automation Status',
    'HEAD: `abc1234`',
    'GitHub Windows Build: `success`',
    'Local CTest count: `51`',
    'Protected untracked entries: `.polaris/, AGENTS.md`',
    'Status readback: `ci=parameter; build=parameter; ctest=parameter`',
    'E2E Rollout Observability Readback',
    'E2E rollout observability: status=`blocked`, ok=`false`, readiness=`blocked`, releaseGate=`production-rollout-observability-blocked-not-linked`, bundle=`json+markdown`',
    'CI: status=`success`, runId=`26816554264`, source=`parameter`; localBuild=`passed`, localCTest=`passed`, count=`51`',
    'Recovery gates: filesystemReady=`true`, filesystemGate=`e2e-filesystem-object-ciphertext-readback-ready`, offlineReady=`true`, offlineGate=`e2e-offline-ciphertext-readback-reviewed-opt-in`',
    'Sensitive export proof: noSensitiveExport=`false`, suppressed=`true`',
    'E2E release evidence package: ok=`true`, releaseReady=`false`, releaseGate=`blocked-ci-head-not-observed`, inputs=`4`',
    'Evidence CI/local: ciStatus=`external-visibility-stale`, ciVisibility=`head-not-observed`, localBuild=`passed`, localCTest=`passed`, count=`2`, noSensitiveExport=`true`',
    'Automation Guardrails',
    'Registered Preview Tasks',
    'Preview task: label=`database-health`, kind=`database-health`, name=`unknown`, display=`Database health`, state=`ok`, format=`qtnetworkchat-database-health-task-preview-v1`, readOnly=`true`, register=`false`, schedule=`Daily@03:15`, path=`',
    'Summary: `Read-only database health check that writes redacted health, status, optional dashboard, last-run, and task history artifacts.`',
    'Preview task: label=`large-file-governance`, kind=`large-file-governance`, name=`unknown`, display=`Large-file governance`, state=`ok`, format=`qtnetworkchat-large-file-governance-task-preview-v1`, readOnly=`true`, register=`false`, schedule=`Hourly/2 h@02:30`, path=`',
    'Summary: `Read-only governance sweep that writes redacted dashboard, reports, diagnostics, last-run, and task history artifacts.`',
    'Automation watch gate: state=`preview-only`, tasks=`2`, registered=`0`, previewOnly=`2`, invalid=`0`, releaseGate=`blocked-preview-only-automation-watch`, action=`register scheduled tasks with -Register or provide registered task artifacts before release`',
    'Scheduled task registry readback: state=`preview-only`, tasks=`2`, expectedRegistered=`0`, found=`0`, missing=`0`, previewOnly=`2`, unreadable=`0`, source=`Get-ScheduledTask`, releaseGate=`blocked-preview-only-automation-watch`, action=`register scheduled tasks with -Register or provide registered task artifacts before release`',
    'Scheduler task: kind=`database-health`, name=`unknown`, expectedRegistered=`false`, readback=`preview-only`, schedulerState=`unknown`, taskPath=`unknown`, source=`preview`',
    'Scheduler task: kind=`large-file-governance`, name=`unknown`, expectedRegistered=`false`, readback=`preview-only`, schedulerState=`unknown`, taskPath=`unknown`, source=`preview`',
    'Generic Task Readback',
    'Generic task readback: `typed task readback active; no unclassified generic tasks`',
    'Scheduled Task Readback',
    'Database health: status=`healthy`, ok=`true`, driver=`QPSQL`, checks=`4`, failedChecks=`0`, slowQueries=`2`, queryFailures=`1`',
    'Gate: readiness=`verified`, releaseGate=`review-query-failures`, action=`Investigate query failures before promoting this database health snapshot.`, auditFocus=`query-failures, slow-queries`',
    'Database health last run: at=`2026-06-03T01:02:03.0000000Z`, exitCode=`0`',
    'Large-file governance: status=`unhealthy`, ok=`false`, warnings=`3`, alerts=`2`, actionableS3Gaps=`1`',
    'Large-file governance last run: at=`2026-06-03T02:03:04.0000000Z`, exitCode=`2`',
    'Task history: runs=`3`, failed=`1`, latestAt=`2026-06-03T03:02:03.0000000Z`, latestExitCode=`0`, acknowledged=`true`, ackExpired=`false`',
    'Task acknowledgement: acknowledged=`true`, by=`oncall-user`, at=`2026-06-03T03:30:00.0000000Z`, reason=`reviewed`',
    'Task acknowledgement gate: state=`failed-acknowledged`, failed=`1`, acknowledged=`true`, ackExpired=`false`, releaseGate=`acknowledged-failure-review-gated`, action=`continue remediation; keep release review gate until failures clear`',
    'Task acknowledgement reminder: state=`acknowledged`, expiryHours=`72`, ageHours=`1.5`, remainingHours=`70.5`, overdueHours=`0`, expiresAt=`2026-06-06T03:30:00.0000000Z`, action=`none`',
    'Artifact Diagnostics',
    'Database health artifacts: `preview=ok; status=ok; lastRun=ok`',
    'Large-file governance artifacts: `preview=ok; status=ok; lastRun=ok`',
    'Automation history artifacts: `history=ok; ack=ok`',
    'E2E rollout observability artifacts: `json=ok; markdown=ok; bundle=json+markdown`',
    'E2E release evidence artifacts: `manifest=ok; releaseGate=blocked-ci-head-not-observed`',
    'Treat mirror branch pushes as explicit per-run opt-ins; the automation status has no fixed secondary branch target.',
    'Priority Backlog',
    'E2E production crypto is the active automation lane again',
    'callable manifest',
    'sanitized execution result contract',
    'linked reviewed builds can pass the early operation, provider control, and explicit reviewed tail probe evidence gates',
    'real public API chain directly for identity generation, public derivation, agreement sign/verify, session derivation, payload encrypt/decrypt, and tamper rejection',
    'Normal provider probe fixtures now use 32-byte valid production material handles',
    'malformed identity handles, malformed verification public keys, and malformed payload keys as invalid-input',
    'explicit reviewed runtime-preflight/arming/execution-acceptance probe readiness',
    'production rotation dry-run/execute evidence',
    'e2e_rollout_observability_exporter now persists sanitized rollout observability JSON/Markdown',
    'default CTest verifies the unlinked fail-closed artifact',
    'linked OpenSSL runtime gate requires accepted evidence before promotion',
    'verified filesystem object readback, explicit reviewed S3 object readback, and explicit reviewed offline mirror readback paths',
    'filesystem object readback and reviewed offline mirror opt-in gates',
    'explicit reviewed S3 object readback, and explicit reviewed offline mirror readback paths',
    'QTNETWORKCHAT_E2E_S3_OBJECT_RECOVERY_REVIEWED=1 plus normal S3 configuration enables reviewed HEAD/GET ciphertext readback',
    'QTNETWORKCHAT_E2E_OFFLINE_OBJECT_RECOVERY_REVIEWED=1 plus QTNETWORKCHAT_E2E_OFFLINE_OBJECT_RECOVERY_ROOT enables canonical safe-token ciphertext readback',
    'S3/offline without reviewed opt-ins expose only safe object-key-token evidence and fixed not-reviewed gates',
    'legacy URL/path-like object locators are suppressed from recovery status',
    'Automation status now consumes the persisted rollout observability JSON/Markdown artifact together with current GitHub Windows Build visibility and local build/CTest readback',
    'remaining E2E release work is external Windows Build visibility recovery and final production-linked release artifact promotion',
    'Group productization is closed for the current automation lane',
    'Mainwindow structure split is no longer the active lane but remains partially complete',
    'README information architecture is closed for now',
    'QTNETWORKCHAT_PGPASSWORD',
    'generated evidence must remain redacted'
)) {
    Assert-Contains -Text $markdown -Expected $expected
}

foreach ($forbidden in @(
    "ghp_",
    "github_pat_",
    "password=super-secret",
    "Authorization:",
    "Credential=",
    "Signature=",
    "chenjun"
)) {
    Assert-NotContains -Text $markdown -Forbidden $forbidden
}
Assert-NoFixedMirrorBranchPolicy -Text $markdown

$autoMarkdownPath = Join-Path $tempDir "automation-status-auto-readback.md"
& $ScriptPath `
    -MarkdownPath $autoMarkdownPath `
    -Head "auto1234567890abcdef" `
    -TrackedRemoteHash "auto1234567890abcdef" `
    -BuildDir $tempDir `
    -LocalVerificationStatusPath $localVerificationPath `
    -GitHubRunListJsonPath $autoRunListPath `
    -CTestLogPath $autoCTestLogPath `
    -DatabaseHealthStatusPath $dbStatusPath `
    -DatabaseHealthLastRunPath $dbLastRunPath `
    -DatabaseHealthTaskPreviewPath $dbPreviewPath `
    -LargeFileGovernanceStatusPath $govStatusPath `
    -LargeFileGovernanceLastRunPath $govLastRunPath `
    -LargeFileGovernanceTaskPreviewPath $govPreviewPath `
    -AutomationTaskHistoryPath $taskHistoryPath `
    -AutomationTaskAckPath $taskAckPath `
    -FailOnSensitive
$autoMarkdown = Get-Content -LiteralPath $autoMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'HEAD: `auto1234567890abcdef`',
    'GitHub Windows Build: `success`',
    'GitHub run id: `99112233`',
    'Local MinGW build: `passed`',
    'Local CTest: `passed`',
    'Local CTest count: `2`',
    'Status readback: `ci=json-artifact; build=local-verification-status; ctest=local-verification-status`'
)) {
    Assert-Contains -Text $autoMarkdown -Expected $expected
}

$artifactCiStatusPath = Join-Path $tempDir "github-windows-build-status-auto.json"
& (Join-Path $PSScriptRoot "write-github-windows-build-status.ps1") `
    -OutputPath $artifactCiStatusPath `
    -Head "auto1234567890abcdef" `
    -RunListJsonPath $autoRunListPath `
    -FailOnSensitive | Out-Null
$artifactCiStatusJson = Get-Content -LiteralPath $artifactCiStatusPath -Raw -Encoding UTF8
$artifactCiStatus = $artifactCiStatusJson | ConvertFrom-Json
if ($artifactCiStatus.format -ne "qtnetworkchat-github-windows-build-status-v1" `
        -or $artifactCiStatus.status -ne "success" `
        -or $artifactCiStatus.runId -ne "99112233" `
        -or $artifactCiStatus.visibility -ne "current-head-observed" `
        -or [int]$artifactCiStatus.observedRunCount -ne 1 `
        -or $artifactCiStatus.latestObserved.headSha -ne "auto1234567890abcdef") {
    throw "GitHub Windows Build status artifact did not preserve the sanitized current-head readback contract."
}
Assert-NotContains -Text $artifactCiStatusJson -Forbidden "feat: auto status"

$artifactCiMarkdownPath = Join-Path $tempDir "automation-status-ci-artifact-readback.md"
& $ScriptPath `
    -MarkdownPath $artifactCiMarkdownPath `
    -Head "auto1234567890abcdef" `
    -TrackedRemoteHash "auto1234567890abcdef" `
    -BuildDir $tempDir `
    -LocalVerificationStatusPath $localVerificationPath `
    -GitHubWindowsBuildStatusPath $artifactCiStatusPath `
    -CTestLogPath $autoCTestLogPath `
    -AutomationTaskHistoryPath $taskHistoryPath `
    -AutomationTaskAckPath $taskAckPath `
    -FailOnSensitive
$artifactCiMarkdown = Get-Content -LiteralPath $artifactCiMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'HEAD: `auto1234567890abcdef`',
    'GitHub Windows Build: `success`',
    'GitHub run id: `99112233`',
    'Status readback: `ci=json-artifact/current-head-observed; build=local-verification-status; ctest=local-verification-status`'
)) {
    Assert-Contains -Text $artifactCiMarkdown -Expected $expected
}

$fallbackMarkdownPath = Join-Path $tempDir "automation-status-fallback-readback.md"
& $ScriptPath `
    -MarkdownPath $fallbackMarkdownPath `
    -Head "fallback1234567890abcdef" `
    -TrackedRemoteHash "fallback1234567890abcdef" `
    -BuildDir $tempDir `
    -LocalVerificationStatusPath (Join-Path $tempDir "missing-local-verification-status.json") `
    -GitHubRunListJsonPath $autoRunListPath `
    -CTestLogPath $autoCTestLogPath `
    -DatabaseHealthStatusPath $dbStatusPath `
    -DatabaseHealthLastRunPath $dbLastRunPath `
    -DatabaseHealthTaskPreviewPath $dbPreviewPath `
    -LargeFileGovernanceStatusPath $govStatusPath `
    -LargeFileGovernanceLastRunPath $govLastRunPath `
    -LargeFileGovernanceTaskPreviewPath $govPreviewPath `
    -AutomationTaskHistoryPath $taskHistoryPath `
    -AutomationTaskAckPath $taskAckPath `
    -FailOnSensitive
$fallbackMarkdown = Get-Content -LiteralPath $fallbackMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'HEAD: `fallback1234567890abcdef`',
    'Local MinGW build: `missing-executable`',
    'Local CTest: `passed`',
    'Local CTest count: `2`',
    'Status readback: `ci=json-artifact; build=auto-build-artifact; ctest=auto-ctest-last-log`'
)) {
    Assert-Contains -Text $fallbackMarkdown -Expected $expected
}

$staleRunListPath = Join-Path $tempDir "gh-run-list-stale.json"
@'
[
  {
    "databaseId": 99112234,
    "headSha": "older1234567890abcdef",
    "status": "completed",
    "conclusion": "failure",
    "createdAt": "2026-06-03T03:00:00Z",
    "displayTitle": "feat: stale",
    "workflowName": "Windows Build"
  }
]
'@ | Set-Content -LiteralPath $staleRunListPath -Encoding UTF8
$staleMarkdownPath = Join-Path $tempDir "automation-status-stale-ci.md"
& $ScriptPath `
    -MarkdownPath $staleMarkdownPath `
    -Head "newer1234567890abcdef" `
    -TrackedRemoteHash "newer1234567890abcdef" `
    -GitHubRunListJsonPath $staleRunListPath `
    -LocalVerificationStatusPath $localVerificationPath `
    -CTestLogPath $autoCTestLogPath `
    -BuildDir $tempDir `
    -DatabaseHealthStatusPath $dbStatusPath `
    -DatabaseHealthLastRunPath $dbLastRunPath `
    -DatabaseHealthTaskPreviewPath $dbPreviewPath `
    -LargeFileGovernanceStatusPath $govStatusPath `
    -LargeFileGovernanceLastRunPath $govLastRunPath `
    -LargeFileGovernanceTaskPreviewPath $govPreviewPath `
    -AutomationTaskHistoryPath $taskHistoryPath `
    -AutomationTaskAckPath $taskAckPath `
    -FailOnSensitive
$staleOutput = Get-Content -LiteralPath $staleMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'HEAD: `newer1234567890abcdef`',
    'GitHub Windows Build: `external-visibility-stale`',
    'GitHub run id: `unknown`',
    'Status readback: `ci=json-artifact; build=local-verification-status; ctest=local-verification-status`'
)) {
    Assert-Contains -Text $staleOutput -Expected $expected
}

$bootstrapPlanOutput = & powershell -ExecutionPolicy Bypass -File $bootstrapScriptPath `
    -OutputDir (Join-Path $tempDir "bootstrap-plan") `
    -ScheduledTaskReadbackPath (Join-Path $tempDir "bootstrap-plan\scheduled-task-readback.json") `
    -Register `
    -User "SYSTEM" `
    -PlanOnly `
    -FailOnSensitive
foreach ($expected in @(
    '"format":  "qtnetworkchat-automation-task-bootstrap-plan-v1"',
    '"register":  true',
    '"user":  "SYSTEM"',
    '"scheduledTaskReadbackPath":'
)) {
    Assert-Contains -Text ($bootstrapPlanOutput -join "`n") -Expected $expected
}

$defaultBootstrapDir = Join-Path $tempDir "default-bootstrap"
$defaultBootstrapMarkdownPath = Join-Path $tempDir "automation-status-default-bootstrap.md"
& $ScriptPath `
    -MarkdownPath $defaultBootstrapMarkdownPath `
    -Head "boot1234" `
    -OriginMain "boot1234" `
    -CiStatus "success" `
    -BuildStatus "passed" `
    -CTestStatus "passed" `
    -CTestCount 69 `
    -BootstrapDefaultTasks `
    -DefaultTaskOutputDir $defaultBootstrapDir `
    -TaskAckExpiryHours 24 `
    -TaskHistoryRetentionCount 5 `
    -FailOnSensitive

if (-not (Test-Path -LiteralPath $defaultBootstrapMarkdownPath -PathType Leaf)) {
    throw "Default bootstrap automation status Markdown was not created"
}
$defaultBootstrapMarkdown = Get-Content -LiteralPath $defaultBootstrapMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'HEAD: `boot1234`',
    'Preview task: label=`database-health`, kind=`database-health`, name=`QtNetworkChatDatabaseHealth`, display=`Database health`, state=`ok`, format=`qtnetworkchat-database-health-task-preview-v1`, readOnly=`true`, register=`false`, schedule=`Daily@03:15`, path=`',
    'Preview task: label=`large-file-governance`, kind=`large-file-governance`, name=`QtNetworkChatLargeFileGovernance`, display=`Large-file governance`, state=`ok`, format=`qtnetworkchat-large-file-governance-task-preview-v1`, readOnly=`true`, register=`false`, schedule=`Daily@03:00`, path=`',
    'Preview task: label=`generic`, kind=`pgsql-release-acceptance`, name=`QtNetworkChatPgsqlReleaseAcceptance`, display=`PostgreSQL release acceptance`, state=`ok`, format=`qtnetworkchat-pgsql-release-acceptance-task-preview-v1`, readOnly=`true`, register=`false`, schedule=`Daily@04:45`, path=`',
    'Scheduled task registry readback: state=`preview-only`, tasks=`3`, expectedRegistered=`0`, found=`0`, missing=`0`, previewOnly=`3`, unreadable=`0`, source=`artifact`, releaseGate=`blocked-preview-only-automation-watch`, action=`register scheduled tasks with -Register or provide registered task artifacts before release`',
    'Generic task readback: `typed task readback active; no unclassified generic tasks`',
    'PostgreSQL release acceptance: name=`QtNetworkChatPgsqlReleaseAcceptance`, status=`configured/ok=true`, lastRun=`0`, history=`runs=1`, ack=`ack=false`, evidence=`ok`',
    'Database health: status=`configured`, ok=`true`, driver=`QPSQL`, checks=`0`, failedChecks=`0`, slowQueries=`0`, queryFailures=`0`',
    'Large-file governance: status=`configured`, ok=`true`, warnings=`0`, alerts=`0`, actionableS3Gaps=`0`',
    'Task history: runs=`1`, failed=`0`, latestAt=`',
    'Task acknowledgement: acknowledged=`false`, by=`cleared`, at=`unknown`, reason=`bootstrap-default`',
    'Task acknowledgement gate: state=`passing`, failed=`0`, acknowledged=`false`, ackExpired=`false`, releaseGate=`passing`, action=`none`',
    'Database health artifacts: `preview=ok; status=ok; lastRun=ok`',
    'Large-file governance artifacts: `preview=ok; status=ok; lastRun=ok`',
    'Automation history artifacts: `history=ok; ack=ok`',
    'Treat mirror branch pushes as explicit per-run opt-ins; the automation status has no fixed secondary branch target.'
)) {
    Assert-Contains -Text $defaultBootstrapMarkdown -Expected $expected
}
foreach ($forbidden in @(
    'Registered preview tasks: `none`',
    'Generic task readback: `none`',
    'PostgreSQL release acceptance: `not configured`',
    'Database health: `not configured`',
    'Large-file governance: `not configured`',
    'Task history: `not configured`',
    "ghp_",
    "github_pat_",
    "Authorization:",
    "Credential=",
    "Signature="
)) {
    Assert-NotContains -Text $defaultBootstrapMarkdown -Forbidden $forbidden
}
Assert-NoFixedMirrorBranchPolicy -Text $defaultBootstrapMarkdown

$planOutput = & $ScriptPath `
    -PlanOnly `
    -Head "def5678" `
    -CiStatus "success" `
    -DatabaseHealthStatusPath (Join-Path $tempDir "missing-database-health-status.json") `
    -DatabaseHealthLastRunPath (Join-Path $tempDir "missing-database-health-last-run.log") `
    -LargeFileGovernanceStatusPath (Join-Path $tempDir "missing-large-file-governance-status.json") `
    -LargeFileGovernanceLastRunPath (Join-Path $tempDir "missing-large-file-governance-last-run.log") `
    -AutomationTaskHistoryPath (Join-Path $tempDir "missing-automation-task-history.json") `
    -FailOnSensitive

foreach ($expected in @(
    'HEAD: `def5678`',
    'Tracked remote branch: `origin/main`',
    'Tracked remote hash: `unknown`',
    'Database health: `configured but status artifact unavailable`',
    'Large-file governance: `configured but status artifact unavailable`',
    'Task history: `configured but history artifact unavailable`',
    'Task acknowledgement gate: state=`history-unavailable`, failed=`unknown`, acknowledged=`unknown`, ackExpired=`unknown`, releaseGate=`automation-task-history-unavailable`, action=`restore automation task history artifact before release`',
    'Database health artifacts: `preview=not-configured; status=missing',
    'Large-file governance artifacts: `preview=not-configured; status=missing',
    'Automation history artifacts: `history=missing',
    'Treat mirror branch pushes as explicit per-run opt-ins; the automation status has no fixed secondary branch target.'
)) {
    Assert-Contains -Text $planOutput -Expected $expected
}
Assert-NoFixedMirrorBranchPolicy -Text $planOutput

$configuredMarkdownPath = Join-Path $configuredTempDir "automation-status.md"
$configuredDbPreviewPath = Join-Path $configuredTempDir "database-health-task-preview.json"
$configuredGovPreviewPath = Join-Path $configuredTempDir "large-file-governance-task-preview.json"
$registeredTaskReadbackPath = Join-Path $configuredTempDir "registered-task-readback.json"
$missingTaskReadbackPath = Join-Path $configuredTempDir "missing-task-readback.json"
Ensure-Directory -Path $configuredTempDir

([ordered]@{
    format = "qtnetworkchat-database-health-task-preview-v1"
    taskKind = "database-health"
    taskDisplayName = "Database health"
    statusArtifactPath = (Join-Path $configuredTempDir "missing-database-health-status.json")
    lastRunPath = (Join-Path $configuredTempDir "missing-database-health-last-run.log")
    historyArtifactPath = (Join-Path $configuredTempDir "missing-automation-task-history.json")
    ackArtifactPath = (Join-Path $configuredTempDir "missing-automation-task-ack.json")
    taskSummary = "configured missing database health sample"
    readOnly = $true
    register = $false
    schedule = "Daily"
    at = "03:15"
    artifactRoles = [ordered]@{
        status = "statusArtifactPath"
        lastRun = "lastRunPath"
        history = "historyArtifactPath"
        ack = "ackArtifactPath"
    }
} | ConvertTo-Json) | Set-Content -LiteralPath $configuredDbPreviewPath -Encoding UTF8

([ordered]@{
    format = "qtnetworkchat-large-file-governance-task-preview-v1"
    taskKind = "large-file-governance"
    taskDisplayName = "Large-file governance"
    statusArtifactPath = (Join-Path $configuredTempDir "missing-large-file-governance-status.json")
    lastRunPath = (Join-Path $configuredTempDir "missing-large-file-governance-last-run.log")
    historyArtifactPath = (Join-Path $configuredTempDir "missing-automation-task-history.json")
    ackArtifactPath = (Join-Path $configuredTempDir "missing-automation-task-ack.json")
    taskSummary = "configured missing large-file governance sample"
    readOnly = $true
    register = $false
    schedule = "Daily"
    at = "03:00"
    artifactRoles = [ordered]@{
        status = "statusArtifactPath"
        lastRun = "lastRunPath"
        history = "historyArtifactPath"
        ack = "ackArtifactPath"
    }
} | ConvertTo-Json) | Set-Content -LiteralPath $configuredGovPreviewPath -Encoding UTF8

& $ScriptPath `
    -MarkdownPath $configuredMarkdownPath `
    -Head "fedcba9" `
    -OriginMain "fedcba9" `
    -CiStatus "queued" `
    -BuildStatus "passed" `
    -CTestStatus "passed" `
    -CTestCount 53 `
    -DatabaseHealthTaskPreviewPath $configuredDbPreviewPath `
    -LargeFileGovernanceTaskPreviewPath $configuredGovPreviewPath `
    -FailOnSensitive

$configuredMarkdown = Get-Content -LiteralPath $configuredMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'Database health: `configured but status artifact unavailable`',
    'Large-file governance: `configured but status artifact unavailable`',
    'Task history: `configured but history artifact unavailable`',
    'Task acknowledgement: `configured but ack artifact unavailable`',
    'Task acknowledgement gate: state=`history-unavailable`, failed=`unknown`, acknowledged=`unknown`, ackExpired=`unknown`, releaseGate=`automation-task-history-unavailable`, action=`restore automation task history artifact before release`',
    'Preview task: label=`database-health`, kind=`database-health`, name=`unknown`, display=`Database health`, state=`ok`, format=`qtnetworkchat-database-health-task-preview-v1`, readOnly=`true`, register=`false`, schedule=`Daily@03:15`',
    'Preview task: label=`large-file-governance`, kind=`large-file-governance`, name=`unknown`, display=`Large-file governance`, state=`ok`, format=`qtnetworkchat-large-file-governance-task-preview-v1`, readOnly=`true`, register=`false`, schedule=`Daily@03:00`',
    'Database health artifacts: `preview=ok; status=missing',
    'Large-file governance artifacts: `preview=ok; status=missing',
    'Automation history artifacts: `history=missing'
)) {
    Assert-Contains -Text $configuredMarkdown -Expected $expected
}

$invalidMarkdownPath = Join-Path $configuredTempDir "automation-status-invalid.md"
$invalidDbPreviewPath = Join-Path $configuredTempDir "database-health-invalid-preview.json"
$invalidGovPreviewPath = Join-Path $configuredTempDir "large-file-governance-invalid-preview.json"
Ensure-Directory -Path $configuredTempDir

'{"taskKind":"database-health","artifactRoles":{"status":"statusArtifactPath","lastRun":"lastRunPath"}}' |
    Set-Content -LiteralPath $invalidDbPreviewPath -Encoding UTF8
'not-json' | Set-Content -LiteralPath $invalidGovPreviewPath -Encoding UTF8
'{"broken":' | Set-Content -LiteralPath (Join-Path $configuredTempDir "broken-history.json") -Encoding UTF8

& $ScriptPath `
    -MarkdownPath $invalidMarkdownPath `
    -Head "1122334" `
    -OriginMain "1122334" `
    -CiStatus "queued" `
    -BuildStatus "passed" `
    -CTestStatus "passed" `
    -CTestCount 54 `
    -DatabaseHealthTaskPreviewPath $invalidDbPreviewPath `
    -LargeFileGovernanceTaskPreviewPath $invalidGovPreviewPath `
    -AutomationTaskHistoryPath (Join-Path $configuredTempDir "broken-history.json") `
    -FailOnSensitive

$invalidMarkdown = Get-Content -LiteralPath $invalidMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'Database health artifacts: `preview=ok; status=preview-role-without-path; lastRun=preview-role-without-path`',
    'Large-file governance artifacts: `preview=invalid-json; status=preview-invalid-json; lastRun=preview-invalid-json`',
    'Automation history artifacts: `history=invalid-json; ack=preview-invalid-json`'
)) {
    Assert-Contains -Text $invalidMarkdown -Expected $expected
}

$genericMarkdownPath = Join-Path $configuredTempDir "automation-status-generic.md"
Ensure-Directory -Path $configuredTempDir
& $ScriptPath `
    -MarkdownPath $genericMarkdownPath `
    -Head "5566778" `
    -OriginMain "5566778" `
    -CiStatus "success" `
    -BuildStatus "passed" `
    -CTestStatus "passed" `
    -CTestCount 54 `
    -TaskPreviewPath @($dbPreviewPath, $govPreviewPath) `
    -FailOnSensitive

$genericMarkdown = Get-Content -LiteralPath $genericMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'Preview task: label=`generic`, kind=`database-health`, name=`unknown`, display=`Database health`, state=`ok`, format=`qtnetworkchat-database-health-task-preview-v1`, readOnly=`true`, register=`false`, schedule=`Daily@03:15`, path=`',
    'Preview task: label=`generic`, kind=`large-file-governance`, name=`unknown`, display=`Large-file governance`, state=`ok`, format=`qtnetworkchat-large-file-governance-task-preview-v1`, readOnly=`true`, register=`false`, schedule=`Hourly/2 h@02:30`, path=`',
    'Generic task readback: `typed task readback active; no unclassified generic tasks`',
    'Database health: status=`healthy`, ok=`true`, driver=`QPSQL`',
    'Gate: readiness=`verified`, releaseGate=`review-query-failures`, action=`Investigate query failures before promoting this database health snapshot.`, auditFocus=`query-failures, slow-queries`',
    'Large-file governance: status=`unhealthy`, ok=`false`, warnings=`3`, alerts=`2`, actionableS3Gaps=`1`',
    'Automation history artifacts: `history=ok; ack=ok`'
)) {
    Assert-Contains -Text $genericMarkdown -Expected $expected
}

$customPreviewPath = Join-Path $configuredTempDir "custom-task-preview.json"
$customStatusPath = Join-Path $configuredTempDir "custom-task-status.json"
$customLastRunPath = Join-Path $configuredTempDir "custom-task-last-run.log"
$customHistoryPath = Join-Path $configuredTempDir "custom-task-history.json"
$customAckPath = Join-Path $configuredTempDir "custom-task-ack.json"
$customMarkdownPath = Join-Path $configuredTempDir "automation-status-custom.md"
$pgsqlSmokePreviewPath = Join-Path $configuredTempDir "pgsql-smoke-task-preview.json"
$pgsqlSmokeStatusPath = Join-Path $configuredTempDir "pgsql-smoke-task-status.json"
$pgsqlSmokeLastRunPath = Join-Path $configuredTempDir "pgsql-smoke-task-last-run.log"
$pgsqlSmokeHistoryPath = Join-Path $configuredTempDir "pgsql-smoke-task-history.json"
$pgsqlSmokeAckPath = Join-Path $configuredTempDir "pgsql-smoke-task-ack.json"
$pgsqlMigrationPreviewPath = Join-Path $configuredTempDir "pgsql-migration-task-preview.json"
$pgsqlMigrationStatusPath = Join-Path $configuredTempDir "pgsql-migration-task-status.json"
$pgsqlMigrationLastRunPath = Join-Path $configuredTempDir "pgsql-migration-task-last-run.log"
$pgsqlMigrationHistoryPath = Join-Path $configuredTempDir "pgsql-migration-task-history.json"
$pgsqlMigrationAckPath = Join-Path $configuredTempDir "pgsql-migration-task-ack.json"
Ensure-Directory -Path $configuredTempDir

@'
{
  "status":"warning",
  "ok":false
}
'@ | Set-Content -LiteralPath $customStatusPath -Encoding UTF8
'2026-06-03T06:00:00.0000000Z exitCode=5' | Set-Content -LiteralPath $customLastRunPath -Encoding UTF8
@'
{
  "runCount":7,
  "failedRunCount":1,
  "acknowledged":false,
  "ackExpired":false,
  "ackReminder":"acknowledge-required",
  "ackExpiryHours":72,
  "ackAgeHours":"unknown",
  "ackHoursRemaining":"unknown",
  "ackHoursOverdue":"unknown",
  "ackExpiresAt":"unknown"
}
'@ | Set-Content -LiteralPath $customHistoryPath -Encoding UTF8
@'
{
  "acknowledged":false
}
'@ | Set-Content -LiteralPath $customAckPath -Encoding UTF8
([ordered]@{
    taskKind = "custom-ops"
    taskName = "CustomOpsTask"
    taskDisplayName = "Custom ops"
    format = "qtnetworkchat-custom-task-preview-v1"
    taskSummary = "Custom generic task sample"
    readOnly = $false
    register = $true
    schedule = "Daily"
    at = "05:45"
    statusArtifactPath = $customStatusPath
    lastRunPath = $customLastRunPath
    historyArtifactPath = $customHistoryPath
    ackArtifactPath = $customAckPath
    artifactRoles = [ordered]@{
        status = "statusArtifactPath"
        lastRun = "lastRunPath"
        history = "historyArtifactPath"
        ack = "ackArtifactPath"
    }
} | ConvertTo-Json) | Set-Content -LiteralPath $customPreviewPath -Encoding UTF8
@'
{
  "status":"success",
  "ok":true,
  "summary":{"readiness":"verified","operatorAction":"Archive the evidence bundle before promoting PostgreSQL smoke coverage."},
  "auditSummary":{"releaseGate":"can-review-smoke-evidence","bootstrapRequired":true,"auditFocus":["offline-attachments","resume-boundaries"],"evidenceBundle":["pgsql-smoke.json","pgsql-smoke.md"]},
  "recoverySummary":{"releaseHint":"review-recovery-summary"}
}
'@ | Set-Content -LiteralPath $pgsqlSmokeStatusPath -Encoding UTF8
'2026-06-03T06:10:00.0000000Z exitCode=0' | Set-Content -LiteralPath $pgsqlSmokeLastRunPath -Encoding UTF8
@'
{
  "runCount":3
}
'@ | Set-Content -LiteralPath $pgsqlSmokeHistoryPath -Encoding UTF8
@'
{
  "acknowledged":true,
  "acknowledgedBy":"oncall",
  "reason":"reviewed"
}
'@ | Set-Content -LiteralPath $pgsqlSmokeAckPath -Encoding UTF8
([ordered]@{
    taskKind = "pgsql-smoke"
    taskName = "PgsqlSmokeTask"
    taskDisplayName = "PostgreSQL smoke"
    format = "qtnetworkchat-pgsql-smoke-task-preview-v1"
    taskSummary = "Real QPSQL smoke gate sample"
    readOnly = $true
    register = $true
    schedule = "Hourly/6 h"
    at = "04:20"
    statusArtifactPath = $pgsqlSmokeStatusPath
    lastRunPath = $pgsqlSmokeLastRunPath
    historyArtifactPath = $pgsqlSmokeHistoryPath
    ackArtifactPath = $pgsqlSmokeAckPath
    artifactRoles = [ordered]@{
        status = "statusArtifactPath"
        lastRun = "lastRunPath"
        history = "historyArtifactPath"
        ack = "ackArtifactPath"
    }
} | ConvertTo-Json) | Set-Content -LiteralPath $pgsqlSmokePreviewPath -Encoding UTF8
@'
{
  "mode":"diff",
  "reportSummary":{"executionReadiness":"review","operatorAction":"Inspect drift tables and rollback preview before cutover."},
  "auditSummary":{"releaseGate":"can-cutover-after-smoke","writeIntent":"read-only-diff","backupRequired":false,"rollbackPreviewAvailable":true,"auditFocus":["drift-report","rollback-preview"],"evidenceBundle":["migration.json","migration.md","migration.html"]}
}
'@ | Set-Content -LiteralPath $pgsqlMigrationStatusPath -Encoding UTF8
'2026-06-03T06:20:00.0000000Z exitCode=2' | Set-Content -LiteralPath $pgsqlMigrationLastRunPath -Encoding UTF8
@'
{
  "runCount":5
}
'@ | Set-Content -LiteralPath $pgsqlMigrationHistoryPath -Encoding UTF8
@'
{
  "acknowledged":false
}
'@ | Set-Content -LiteralPath $pgsqlMigrationAckPath -Encoding UTF8
([ordered]@{
    taskKind = "pgsql-migration"
    taskName = "PgsqlMigrationTask"
    taskDisplayName = "PostgreSQL migration"
    format = "qtnetworkchat-pgsql-migration-task-preview-v1"
    taskSummary = "SQLite to PostgreSQL release gate sample"
    readOnly = $true
    register = $true
    schedule = "Daily"
    at = "06:40"
    statusArtifactPath = $pgsqlMigrationStatusPath
    lastRunPath = $pgsqlMigrationLastRunPath
    historyArtifactPath = $pgsqlMigrationHistoryPath
    ackArtifactPath = $pgsqlMigrationAckPath
    artifactRoles = [ordered]@{
        status = "statusArtifactPath"
        lastRun = "lastRunPath"
        history = "historyArtifactPath"
        ack = "ackArtifactPath"
    }
} | ConvertTo-Json) | Set-Content -LiteralPath $pgsqlMigrationPreviewPath -Encoding UTF8

([ordered]@{
    format = "qtnetworkchat-scheduled-task-readback-v1"
    tasks = @(
        [ordered]@{
            taskName = "CustomOpsTask"
            registered = $true
            schedulerState = "Ready"
            taskPath = "\QtNetworkChat\"
        },
        [ordered]@{
            taskName = "PgsqlSmokeTask"
            registered = $true
            schedulerState = "Ready"
            taskPath = "\QtNetworkChat\"
        },
        [ordered]@{
            taskName = "PgsqlMigrationTask"
            registered = $true
            schedulerState = "Disabled"
            taskPath = "\QtNetworkChat\"
        }
    )
} | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $registeredTaskReadbackPath -Encoding UTF8

& $ScriptPath `
    -MarkdownPath $customMarkdownPath `
    -Head "8899aa0" `
    -OriginMain "8899aa0" `
    -CiStatus "success" `
    -BuildStatus "passed" `
    -CTestStatus "passed" `
    -CTestCount 54 `
    -TaskPreviewPath @($customPreviewPath, $pgsqlSmokePreviewPath, $pgsqlMigrationPreviewPath) `
    -ScheduledTaskReadbackJsonPath $registeredTaskReadbackPath `
    -FailOnSensitive

$customMarkdown = Get-Content -LiteralPath $customMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'Preview task: label=`generic`, kind=`custom-ops`, name=`CustomOpsTask`, display=`Custom ops`, state=`ok`, format=`qtnetworkchat-custom-task-preview-v1`, readOnly=`false`, register=`true`, schedule=`Daily@05:45`, path=`',
    'Preview task: label=`generic`, kind=`pgsql-smoke`, name=`PgsqlSmokeTask`, display=`PostgreSQL smoke`, state=`ok`, format=`qtnetworkchat-pgsql-smoke-task-preview-v1`, readOnly=`true`, register=`true`, schedule=`Hourly/6 h@04:20`, path=`',
    'Preview task: label=`generic`, kind=`pgsql-migration`, name=`PgsqlMigrationTask`, display=`PostgreSQL migration`, state=`ok`, format=`qtnetworkchat-pgsql-migration-task-preview-v1`, readOnly=`true`, register=`true`, schedule=`Daily@06:40`, path=`',
    'Automation watch gate: state=`registered-ack-gated`, tasks=`3`, registered=`3`, previewOnly=`0`, invalid=`0`, releaseGate=`blocked-unacknowledged-failure`, action=`acknowledge failed automation task before release`',
    'Scheduled task registry readback: state=`registered`, tasks=`3`, expectedRegistered=`3`, found=`3`, missing=`0`, previewOnly=`0`, unreadable=`0`, source=`artifact`, releaseGate=`scheduled-task-readback-registered`, action=`verify scheduler run history stays fresh before release`',
    'Scheduler task: kind=`custom-ops`, name=`CustomOpsTask`, expectedRegistered=`true`, readback=`registered`, schedulerState=`Ready`, taskPath=`\QtNetworkChat\`, source=`artifact`',
    'Scheduler task: kind=`pgsql-smoke`, name=`PgsqlSmokeTask`, expectedRegistered=`true`, readback=`registered`, schedulerState=`Ready`, taskPath=`\QtNetworkChat\`, source=`artifact`',
    'Scheduler task: kind=`pgsql-migration`, name=`PgsqlMigrationTask`, expectedRegistered=`true`, readback=`registered`, schedulerState=`Disabled`, taskPath=`\QtNetworkChat\`, source=`artifact`',
    'Summary: `Custom generic task sample`',
    'Generic task: kind=`custom-ops`, name=`CustomOpsTask`, display=`Custom ops`, status=`warning/ok=false`, lastRun=`5`, history=`runs=7`, ack=`ack=false`',
    'Generic task: kind=`pgsql-smoke`, name=`PgsqlSmokeTask`, display=`PostgreSQL smoke`, status=`success/ok=true`, lastRun=`0`, history=`runs=3`, ack=`ack=true`',
    'Gate: readiness=`verified`, releaseGate=`can-review-smoke-evidence`, action=`Archive the evidence bundle before promoting PostgreSQL smoke coverage.`, auditFocus=`offline-attachments, resume-boundaries`',
    'Release details: `bootstrapRequired=true; releaseHint=review-recovery-summary; evidence=pgsql-smoke.json, pgsql-smoke.md`',
    'Summary: `Real QPSQL smoke gate sample`',
    'Generic task: kind=`pgsql-migration`, name=`PgsqlMigrationTask`, display=`PostgreSQL migration`, status=`ok`, lastRun=`2`, history=`runs=5`, ack=`ack=false`',
    'Gate: readiness=`review`, releaseGate=`can-cutover-after-smoke`, action=`Inspect drift tables and rollback preview before cutover.`, auditFocus=`drift-report, rollback-preview`',
    'Release details: `writeIntent=read-only-diff; backupRequired=false; rollbackPreview=true; evidence=migration.json, migration.md, migration.html`',
    'Summary: `SQLite to PostgreSQL release gate sample`',
    'Database health: `configured but status artifact unavailable`',
    'Large-file governance: `configured but status artifact unavailable`',
    'Task history: runs=`7`',
    'Task acknowledgement: acknowledged=`false`, by=`unknown`, at=`unknown`, reason=`unknown`',
    'Task acknowledgement gate: state=`failed-unacknowledged`, failed=`1`, acknowledged=`false`, ackExpired=`false`, releaseGate=`blocked-unacknowledged-failure`, action=`acknowledge failed automation task before release`',
    'Task acknowledgement reminder: state=`acknowledge-required`, expiryHours=`72`, ageHours=`unknown`, remainingHours=`unknown`, overdueHours=`unknown`, expiresAt=`unknown`, action=`acknowledge failed automation task before release`',
    'Treat mirror branch pushes as explicit per-run opt-ins; the automation status has no fixed secondary branch target.'
)) {
    Assert-Contains -Text $customMarkdown -Expected $expected
}
Assert-NoFixedMirrorBranchPolicy -Text $customMarkdown

([ordered]@{
    format = "qtnetworkchat-scheduled-task-readback-v1"
    tasks = @(
        [ordered]@{
            taskName = "CustomOpsTask"
            registered = $true
            schedulerState = "Ready"
            taskPath = "\QtNetworkChat\"
        }
    )
} | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $missingTaskReadbackPath -Encoding UTF8
$missingTaskMarkdownPath = Join-Path $configuredTempDir "automation-status-missing-scheduled-task.md"
& $ScriptPath `
    -MarkdownPath $missingTaskMarkdownPath `
    -Head "8899ab1" `
    -OriginMain "8899ab1" `
    -CiStatus "success" `
    -BuildStatus "passed" `
    -CTestStatus "passed" `
    -CTestCount 54 `
    -TaskPreviewPath @($customPreviewPath, $pgsqlSmokePreviewPath) `
    -ScheduledTaskReadbackJsonPath $missingTaskReadbackPath `
    -FailOnSensitive

$missingTaskMarkdown = Get-Content -LiteralPath $missingTaskMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'Automation watch gate: state=`registered-missing`, tasks=`2`, registered=`2`, previewOnly=`0`, invalid=`0`, releaseGate=`blocked-scheduled-task-missing`, action=`restore missing scheduled tasks before release`',
    'Scheduled task registry readback: state=`registered-missing`, tasks=`2`, expectedRegistered=`2`, found=`1`, missing=`1`, previewOnly=`0`, unreadable=`0`, source=`artifact`, releaseGate=`blocked-scheduled-task-missing`, action=`restore missing scheduled tasks before release`',
    'Scheduler task: kind=`custom-ops`, name=`CustomOpsTask`, expectedRegistered=`true`, readback=`registered`, schedulerState=`Ready`, taskPath=`\QtNetworkChat\`, source=`artifact`',
    'Scheduler task: kind=`pgsql-smoke`, name=`PgsqlSmokeTask`, expectedRegistered=`true`, readback=`missing`, schedulerState=`unknown`, taskPath=`unknown`, source=`artifact`'
)) {
    Assert-Contains -Text $missingTaskMarkdown -Expected $expected
}
Assert-NoFixedMirrorBranchPolicy -Text $missingTaskMarkdown

$expiredHistoryPath = Join-Path $configuredTempDir "expired-task-history.json"
$expiredAckPath = Join-Path $configuredTempDir "expired-task-ack.json"
$expiredMarkdownPath = Join-Path $configuredTempDir "automation-status-expired-ack.md"
@'
{
  "format":"qtnetworkchat-automation-task-history-v1",
  "runCount":2,
  "failedRunCount":1,
  "latestRun":{"timestamp":"2026-06-03T07:00:00.0000000Z","exitCode":2},
  "acknowledged":false,
  "ackExpired":true,
  "ackReminder":"renew-required",
  "ackExpiryHours":72,
  "ackAgeHours":96,
  "ackHoursRemaining":0,
  "ackHoursOverdue":24,
  "ackExpiresAt":"2026-06-04T07:00:00.0000000Z"
}
'@ | Set-Content -LiteralPath $expiredHistoryPath -Encoding UTF8
@'
{
  "format":"qtnetworkchat-automation-task-ack-v1",
  "acknowledged":true,
  "acknowledgedBy":"oncall",
  "acknowledgedAt":"2026-06-01T07:00:00.0000000Z",
  "reason":"expired sample"
}
'@ | Set-Content -LiteralPath $expiredAckPath -Encoding UTF8

& $ScriptPath `
    -MarkdownPath $expiredMarkdownPath `
    -Head "aabbcc0" `
    -OriginMain "aabbcc0" `
    -CiStatus "success" `
    -BuildStatus "passed" `
    -CTestStatus "passed" `
    -CTestCount 54 `
    -AutomationTaskHistoryPath $expiredHistoryPath `
    -AutomationTaskAckPath $expiredAckPath `
    -FailOnSensitive

$expiredMarkdown = Get-Content -LiteralPath $expiredMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'Task history: runs=`2`, failed=`1`, latestAt=`2026-06-03T07:00:00.0000000Z`, latestExitCode=`2`, acknowledged=`false`, ackExpired=`true`',
    'Task acknowledgement: acknowledged=`true`, by=`oncall`, at=`2026-06-01T07:00:00.0000000Z`, reason=`expired sample`',
    'Task acknowledgement gate: state=`failed-ack-expired`, failed=`1`, acknowledged=`false`, ackExpired=`true`, releaseGate=`blocked-ack-expired`, action=`renew task acknowledgement before release`',
    'Task acknowledgement reminder: state=`renew-required`, expiryHours=`72`, ageHours=`96`, remainingHours=`0`, overdueHours=`24`, expiresAt=`2026-06-04T07:00:00.0000000Z`, action=`renew expired automation task acknowledgement before release`'
)) {
    Assert-Contains -Text $expiredMarkdown -Expected $expected
}
Assert-NoFixedMirrorBranchPolicy -Text $expiredMarkdown

Remove-Item -Recurse -Force $tempDir, $configuredTempDir -ErrorAction SilentlyContinue
Write-Host "Automation status writer test passed"
