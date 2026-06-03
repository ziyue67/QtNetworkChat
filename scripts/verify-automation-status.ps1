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

$tempDir = Join-Path $PSScriptRoot "..\automation_status_sample"
$configuredTempDir = Join-Path $PSScriptRoot "..\automation_status_configured_missing_sample"
Remove-Item -Recurse -Force $tempDir, $configuredTempDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $tempDir, $configuredTempDir | Out-Null

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
  "queryMetrics":{"slowQueryCount":2,"queryFailureCount":1}
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
    taskKind = "database-health"
    statusArtifactPath = $dbStatusPath
    lastRunPath = $dbLastRunPath
    historyArtifactPath = $taskHistoryPath
    ackArtifactPath = $taskAckPath
    artifactRoles = [ordered]@{
        status = "statusArtifactPath"
        lastRun = "lastRunPath"
        history = "historyArtifactPath"
        ack = "ackArtifactPath"
    }
} | ConvertTo-Json) | Set-Content -LiteralPath $dbPreviewPath -Encoding UTF8

([ordered]@{
    taskKind = "large-file-governance"
    statusArtifactPath = $govStatusPath
    lastRunPath = $govLastRunPath
    historyArtifactPath = $taskHistoryPath
    ackArtifactPath = $taskAckPath
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
    -OriginCodexQt "abc1234" `
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
    'Preview task: label=`database-health`, kind=`database-health`, name=`unknown`, state=`ok`, path=`',
    'Preview task: label=`large-file-governance`, kind=`large-file-governance`, name=`unknown`, state=`ok`, path=`',
    'Generic Task Readback',
    'Generic task readback: `none`',
    'Scheduled Task Readback',
    'Database health: status=`healthy`, ok=`true`, driver=`QPSQL`, checks=`4`, failedChecks=`0`, slowQueries=`2`, queryFailures=`1`',
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
    'origin/main: `unknown`',
    'origin/codex/qt: `unknown`',
    'Database health: `configured but status artifact unavailable`',
    'Large-file governance: `configured but status artifact unavailable`',
    'Task history: `configured but history artifact unavailable`',
    'Database health artifacts: `preview=not-configured; status=missing',
    'Large-file governance artifacts: `preview=not-configured; status=missing',
    'Automation history artifacts: `history=missing'
)) {
    Assert-Contains -Text $planOutput -Expected $expected
}

$configuredMarkdownPath = Join-Path $configuredTempDir "automation-status.md"
$configuredDbPreviewPath = Join-Path $configuredTempDir "database-health-task-preview.json"
$configuredGovPreviewPath = Join-Path $configuredTempDir "large-file-governance-task-preview.json"

([ordered]@{
    taskKind = "database-health"
    statusArtifactPath = (Join-Path $configuredTempDir "missing-database-health-status.json")
    lastRunPath = (Join-Path $configuredTempDir "missing-database-health-last-run.log")
    historyArtifactPath = (Join-Path $configuredTempDir "missing-automation-task-history.json")
    ackArtifactPath = (Join-Path $configuredTempDir "missing-automation-task-ack.json")
    artifactRoles = [ordered]@{
        status = "statusArtifactPath"
        lastRun = "lastRunPath"
        history = "historyArtifactPath"
        ack = "ackArtifactPath"
    }
} | ConvertTo-Json) | Set-Content -LiteralPath $configuredDbPreviewPath -Encoding UTF8

([ordered]@{
    taskKind = "large-file-governance"
    statusArtifactPath = (Join-Path $configuredTempDir "missing-large-file-governance-status.json")
    lastRunPath = (Join-Path $configuredTempDir "missing-large-file-governance-last-run.log")
    historyArtifactPath = (Join-Path $configuredTempDir "missing-automation-task-history.json")
    ackArtifactPath = (Join-Path $configuredTempDir "missing-automation-task-ack.json")
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
    -OriginCodexQt "fedcba9" `
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
    'Database health artifacts: `preview=ok; status=missing',
    'Large-file governance artifacts: `preview=ok; status=missing',
    'Automation history artifacts: `history=missing'
)) {
    Assert-Contains -Text $configuredMarkdown -Expected $expected
}

$invalidMarkdownPath = Join-Path $configuredTempDir "automation-status-invalid.md"
$invalidDbPreviewPath = Join-Path $configuredTempDir "database-health-invalid-preview.json"
$invalidGovPreviewPath = Join-Path $configuredTempDir "large-file-governance-invalid-preview.json"

'{"taskKind":"database-health","artifactRoles":{"status":"statusArtifactPath","lastRun":"lastRunPath"}}' |
    Set-Content -LiteralPath $invalidDbPreviewPath -Encoding UTF8
'not-json' | Set-Content -LiteralPath $invalidGovPreviewPath -Encoding UTF8
'{"broken":' | Set-Content -LiteralPath (Join-Path $configuredTempDir "broken-history.json") -Encoding UTF8

& $ScriptPath `
    -MarkdownPath $invalidMarkdownPath `
    -Head "1122334" `
    -OriginMain "1122334" `
    -OriginCodexQt "1122334" `
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
& $ScriptPath `
    -MarkdownPath $genericMarkdownPath `
    -Head "5566778" `
    -OriginMain "5566778" `
    -OriginCodexQt "5566778" `
    -CiStatus "success" `
    -BuildStatus "passed" `
    -CTestStatus "passed" `
    -CTestCount 54 `
    -TaskPreviewPath @($dbPreviewPath, $govPreviewPath) `
    -FailOnSensitive

$genericMarkdown = Get-Content -LiteralPath $genericMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'Preview task: label=`generic`, kind=`database-health`, name=`unknown`, state=`ok`, path=`',
    'Preview task: label=`generic`, kind=`large-file-governance`, name=`unknown`, state=`ok`, path=`',
    'Generic task readback: `none`',
    'Database health: status=`healthy`, ok=`true`, driver=`QPSQL`',
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

& $ScriptPath `
    -MarkdownPath $customMarkdownPath `
    -Head "8899aa0" `
    -OriginMain "8899aa0" `
    -OriginCodexQt "8899aa0" `
    -CiStatus "success" `
    -BuildStatus "passed" `
    -CTestStatus "passed" `
    -CTestCount 54 `
    -TaskPreviewPath @($customPreviewPath) `
    -FailOnSensitive

$customMarkdown = Get-Content -LiteralPath $customMarkdownPath -Raw -Encoding UTF8
foreach ($expected in @(
    'Preview task: label=`generic`, kind=`custom-ops`, name=`CustomOpsTask`, state=`ok`, path=`',
    'Generic task: kind=`custom-ops`, name=`CustomOpsTask`, status=`warning/ok=false`, lastRun=`5`, history=`runs=7`, ack=`ack=false`',
    'Database health: `configured but status artifact unavailable`',
    'Large-file governance: `configured but status artifact unavailable`',
    'Task history: runs=`7`',
    'Task acknowledgement: acknowledged=`false`, by=`unknown`, at=`unknown`, reason=`unknown`'
)) {
    Assert-Contains -Text $customMarkdown -Expected $expected
}

Remove-Item -Recurse -Force $tempDir, $configuredTempDir -ErrorAction SilentlyContinue
Write-Host "Automation status writer test passed"
