function Test-QtGovernanceSensitiveText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    $patterns = @(
        'https?://[^\s"]+',
        '(?i)\bendpoint\b\s*[:=]',
        '(?i)\bbucket\b\s*[:=]',
        '(?i)\bobjectUrl\b\s*[:=]',
        '(?i)\baccess[_-]?key\b\s*[:=]',
        '(?i)\bsecret[_-]?key\b\s*[:=]',
        '(?i)\bsession[_-]?token\b\s*[:=]',
        '(?i)\bAuthorization\b',
        '(?i)\bCredential=',
        '(?i)\bSignature='
    )
    foreach ($pattern in $patterns) {
        if ($Text -match $pattern) {
            return $true
        }
    }
    return $false
}

function Assert-QtGovernanceNoSensitiveText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [string]$Message = "Sensitive S3/ObjectStore fields were found."
    )

    if (Test-QtGovernanceSensitiveText -Text $Text) {
        throw $Message
    }
}

function ConvertTo-QtGovernanceJson {
    param(
        [Parameter(Mandatory = $true)]
        [object]$InputObject,
        [int]$Depth = 8
    )

    return ($InputObject | ConvertTo-Json -Depth $Depth)
}
