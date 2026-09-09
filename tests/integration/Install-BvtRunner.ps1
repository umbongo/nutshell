# Install-BvtRunner.ps1 — register this PC as the self-hosted GitHub Actions
# runner that executes the BVT workflow (.github/workflows/bvt.yml).
#
# Why a self-hosted runner: the BVT drives the real nutshell.exe on a Windows
# desktop and connects to the test host (tompi) on the LAN. GitHub-hosted
# runners have neither. The runner is started as an interactive process at
# logon (a scheduled task), not as a Windows service, because a service runs
# in session 0 with no desktop and GUI tests would not have a window to drive.
#
# Prerequisites (one-time, by a person):
#   1. GitHub CLI installed (`winget install GitHub.cli`) and logged in with an
#      account that administers the repository: `gh auth login`.
#   2. The runner package extracted to -RunnerDir (default C:\actions-runner):
#      https://github.com/actions/runner/releases (actions-runner-win-x64-*.zip).
#   3. MSYS2 MINGW64 toolchain at C:\msys64 and the test host reachable with
#      the key in ~\.ssh\thomas (see tests/integration/README.md).
#
# Usage (Windows PowerShell 5.1 or pwsh, from any directory):
#   .\tests\integration\Install-BvtRunner.ps1
#   .\tests\integration\Install-BvtRunner.ps1 -Uninstall
#
# The registration token is fetched with `gh api` and used once; nothing
# secret is stored by this script.

param(
    [string] $Repo = "umbongo/nutshell",
    [string] $RunnerDir = "C:\actions-runner",
    [string] $RunnerName = $env:COMPUTERNAME.ToLower() + "-bvt",
    [string] $Labels = "nutshell-bvt",
    [string] $TaskName = "Nutshell BVT runner",
    [switch] $Uninstall
)

$ErrorActionPreference = "Stop"

function Get-GhPath {
    $cands = @((Get-Command gh -ErrorAction SilentlyContinue | ForEach-Object { $_.Source }),
               "C:\Program Files\GitHub CLI\gh.exe")
    foreach ($c in $cands) { if ($c -and (Test-Path $c)) { return $c } }
    throw "GitHub CLI (gh) not found. Install it with: winget install GitHub.cli"
}

$gh = Get-GhPath
& $gh auth status 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw "gh is not logged in. Run: gh auth login" }

if (-not (Test-Path (Join-Path $RunnerDir "config.cmd"))) {
    throw "Runner package not found in $RunnerDir (expected config.cmd). Extract actions-runner-win-x64-*.zip there first."
}

if ($Uninstall) {
    Write-Host "Removing scheduled task '$TaskName' (if present)..."
    schtasks /Delete /TN $TaskName /F 2>$null | Out-Null
    Write-Host "Fetching a removal token and unregistering the runner..."
    $tok = (& $gh api -X POST "repos/$Repo/actions/runners/remove-token" --jq .token)
    Push-Location $RunnerDir
    try { & .\config.cmd remove --token $tok } finally { Pop-Location }
    Write-Host "Done."
    exit 0
}

Write-Host "Fetching a registration token for $Repo..."
$token = (& $gh api -X POST "repos/$Repo/actions/runners/registration-token" --jq .token)
if (-not $token) { throw "could not obtain a registration token (does the account administer $Repo?)" }

Push-Location $RunnerDir
try {
    Write-Host "Configuring runner '$RunnerName' with labels '$Labels'..."
    & .\config.cmd --unattended --url "https://github.com/$Repo" --token $token `
        --name $RunnerName --labels $Labels --work "_work" --replace
    if ($LASTEXITCODE -ne 0) { throw "config.cmd failed ($LASTEXITCODE)" }
} finally { Pop-Location }

# Start the runner at logon as an interactive process (a service would run in
# session 0 without a desktop). It keeps running while the user is logged in;
# a disconnected RDP session is fine, a logged-out one is not.
$runCmd = Join-Path $RunnerDir "run.cmd"
Write-Host "Creating scheduled task '$TaskName' (at logon, interactive)..."
schtasks /Create /F /TN $TaskName /SC ONLOGON /RL LIMITED /TR "`"$runCmd`"" | Out-Null
schtasks /Run /TN $TaskName | Out-Null

Write-Host ""
Write-Host "Runner registered. Check it under https://github.com/$Repo/settings/actions/runners"
Write-Host "It must show as Idle (green) before the BVT workflow can run."
