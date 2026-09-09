# Protect-Main.ps1 — make "BVT passed" the guardrail for main.
#
# Applies a branch protection rule to main so that:
#   - changes reach main only through a pull request (direct pushes rejected,
#     for administrators too),
#   - the pull request cannot merge until the "BVT" status check (the job in
#     .github/workflows/bvt.yml) has succeeded on its latest commit,
#   - the branch must be up to date with main before merging, so the check
#     ran against what will actually land,
#   - force pushes and deletion of main are blocked.
#
# No reviewer approval is required (a one-person project); add
# required_pull_request_reviews if that changes.
#
# Prerequisites: GitHub CLI logged in as a repository administrator.
# Usage:  .\tests\integration\Protect-Main.ps1        (apply)
#         .\tests\integration\Protect-Main.ps1 -Show  (print the current rule)

param(
    [string] $Repo = "umbongo/nutshell",
    [string] $Branch = "main",
    [string] $CheckName = "BVT",
    [switch] $Show
)

$ErrorActionPreference = "Stop"
$gh = (Get-Command gh -ErrorAction SilentlyContinue | ForEach-Object { $_.Source })
if (-not $gh) { $gh = "C:\Program Files\GitHub CLI\gh.exe" }
if (-not (Test-Path $gh)) { throw "GitHub CLI (gh) not found. Install it with: winget install GitHub.cli" }
& $gh auth status 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw "gh is not logged in. Run: gh auth login" }

if ($Show) {
    & $gh api "repos/$Repo/branches/$Branch/protection"
    exit $LASTEXITCODE
}

$body = @{
    required_status_checks = @{
        strict   = $true
        contexts = @($CheckName)
    }
    enforce_admins                 = $true
    required_pull_request_reviews  = $null
    restrictions                   = $null
    allow_force_pushes             = $false
    allow_deletions                = $false
    required_linear_history        = $false
    required_conversation_resolution = $false
} | ConvertTo-Json -Depth 5

$tmp = [IO.Path]::GetTempFileName()
try {
    [IO.File]::WriteAllText($tmp, $body, (New-Object Text.UTF8Encoding $false))
    & $gh api -X PUT "repos/$Repo/branches/$Branch/protection" `
        -H "Accept: application/vnd.github+json" --input $tmp | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "gh api returned $LASTEXITCODE" }
} finally { Remove-Item $tmp -ErrorAction SilentlyContinue }

Write-Host "main is protected: pull requests only, '$CheckName' must pass, branch must be up to date, no force pushes."
