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

function Ensure-Directory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

$tempDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\automation_status_sample"))
$configuredTempDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\automation_status_configured_missing_sample"))
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
  "ackExpired":false
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
    'Automation Guardrails',
    'Registered Preview Tasks',
    'Preview task: label=`database-health`, kind=`database-health`, name=`unknown`, display=`Database health`, state=`ok`, format=`qtnetworkchat-database-health-task-preview-v1`, readOnly=`true`, register=`false`, schedule=`Daily@03:15`, path=`',
    'Summary: `Read-only database health check that writes redacted health, status, optional dashboard, last-run, and task history artifacts.`',
    'Preview task: label=`large-file-governance`, kind=`large-file-governance`, name=`unknown`, display=`Large-file governance`, state=`ok`, format=`qtnetworkchat-large-file-governance-task-preview-v1`, readOnly=`true`, register=`false`, schedule=`Hourly/2 h@02:30`, path=`',
    'Summary: `Read-only governance sweep that writes redacted dashboard, reports, diagnostics, last-run, and task history artifacts.`',
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
    'Artifact Diagnostics',
    'Database health artifacts: `preview=ok; status=ok; lastRun=ok`',
    'Large-file governance artifacts: `preview=ok; status=ok; lastRun=ok`',
    'Automation history artifacts: `history=ok; ack=ok`',
    'Priority Backlog',
    'E2E production crypto is the active automation lane again',
    'callable manifest',
    'sanitized execution result contract',
    'production rotation dry-run/execute evidence',
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
    'Generic task readback: `typed task readback active; no unclassified generic tasks`',
    'PostgreSQL release acceptance: name=`QtNetworkChatPgsqlReleaseAcceptance`, status=`configured/ok=true`, lastRun=`0`, history=`runs=1`, ack=`ack=false`, evidence=`ok`',
    'Database health: status=`configured`, ok=`true`, driver=`QPSQL`, checks=`0`, failedChecks=`0`, slowQueries=`0`, queryFailures=`0`',
    'Large-file governance: status=`configured`, ok=`true`, warnings=`0`, alerts=`0`, actionableS3Gaps=`0`',
    'Task history: runs=`1`, failed=`0`, latestAt=`',
    'Task acknowledgement: acknowledged=`false`, by=`cleared`, at=`unknown`, reason=`bootstrap-default`',
    'Database health artifacts: `preview=ok; status=ok; lastRun=ok`',
    'Large-file governance artifacts: `preview=ok; status=ok; lastRun=ok`',
    'Automation history artifacts: `history=ok; ack=ok`'
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
    'Database health artifacts: `preview=not-configured; status=missing',
    'Large-file governance artifacts: `preview=not-configured; status=missing',
    'Automation history artifacts: `history=missing'
)) {
    Assert-Contains -Text $planOutput -Expected $expected
}
foreach ($forbidden in @(
    'origin/codex/qt',
    'fast-forward codex/qt',
    'push main, then fast-forward codex/qt'
)) {
    Assert-NotContains -Text $planOutput -Forbidden $forbidden
}

$configuredMarkdownPath = Join-Path $configuredTempDir "automation-status.md"
$configuredDbPreviewPath = Join-Path $configuredTempDir "database-health-task-preview.json"
$configuredGovPreviewPath = Join-Path $configuredTempDir "large-file-governance-task-preview.json"
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
  "runCount":7
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

& $ScriptPath `
    -MarkdownPath $customMarkdownPath `
    -Head "8899aa0" `
    -OriginMain "8899aa0" `
    -CiStatus "success" `
    -BuildStatus "passed" `
    -CTestStatus "passed" `
    -CTestCount 54 `
    -TaskPreviewPath @($customPreviewPath, $pgsqlSmokePreviewPath, $pgsqlMigrationPreviewPath) `
    -FailOnSensitive

$customMarkdown = Get-Content -LiteralPath $customMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'Preview task: label=`generic`, kind=`custom-ops`, name=`CustomOpsTask`, display=`Custom ops`, state=`ok`, format=`qtnetworkchat-custom-task-preview-v1`, readOnly=`false`, register=`true`, schedule=`Daily@05:45`, path=`',
    'Preview task: label=`generic`, kind=`pgsql-smoke`, name=`PgsqlSmokeTask`, display=`PostgreSQL smoke`, state=`ok`, format=`qtnetworkchat-pgsql-smoke-task-preview-v1`, readOnly=`true`, register=`true`, schedule=`Hourly/6 h@04:20`, path=`',
    'Preview task: label=`generic`, kind=`pgsql-migration`, name=`PgsqlMigrationTask`, display=`PostgreSQL migration`, state=`ok`, format=`qtnetworkchat-pgsql-migration-task-preview-v1`, readOnly=`true`, register=`true`, schedule=`Daily@06:40`, path=`',
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
    'Task acknowledgement: acknowledged=`false`, by=`unknown`, at=`unknown`, reason=`unknown`'
)) {
    Assert-Contains -Text $customMarkdown -Expected $expected
}
foreach ($forbidden in @(
    'origin/codex/qt',
    'fast-forward codex/qt',
    'push main, then fast-forward codex/qt'
)) {
    Assert-NotContains -Text $customMarkdown -Forbidden $forbidden
}

Remove-Item -Recurse -Force $tempDir, $configuredTempDir -ErrorAction SilentlyContinue
Write-Host "Automation status writer test passed"
