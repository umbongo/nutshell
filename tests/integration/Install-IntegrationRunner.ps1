# Install-IntegrationRunner.ps1 — register this PC as the self-hosted GitHub
# Actions runner that executes the "Integration tests" workflow
# (.github/workflows/integration.yml).
#
# Why a self-hosted runner: the suite drives the real nutshell.exe on a Windows
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
#   .\tests\integration\Install-IntegrationRunner.ps1
#   .\tests\integration\Install-IntegrationRunner.ps1 -Uninstall
#
# The registration token is fetched with `gh api` and used once; nothing
# secret is stored by this script.
#
# Re-labelling a runner that is already registered needs neither this script
# nor the machine -- add the label through the API and it takes effect without
# re-registering:
#
#   gh api -X POST repos/<owner>/<repo>/actions/runners/<id>/labels \
#       -f "labels[]=nutshell-desktop"

param(
    [string] $Repo = "umbongo/nutshell",
    [string] $RunnerDir = "C:\actions-runner",
    [string] $RunnerName = $env:COMPUTERNAME.ToLower() + "-nutshell",
    [string] $Labels = "nutshell-desktop",
    [string] $TaskName = "Nutshell integration runner",
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
    $startupDir = [Environment]::GetFolderPath('Startup')
    # "Nutshell BVT runner" is the pre-rename name: clear it too, or an old
    # shortcut keeps starting a second runner instance at every logon.
    foreach ($name in @($TaskName, "Nutshell BVT runner")) {
        $lnk = Join-Path $startupDir ($name + ".lnk")
        if (Test-Path $lnk) {
            Write-Host "Removing Startup shortcut '$lnk'..."
            Remove-Item $lnk -ErrorAction SilentlyContinue
        }
    }
    Write-Host "Stop the running runner window yourself (run.cmd) before or after this."
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
# session 0 without a desktop). A shortcut in the user's Startup folder does
# this without administrator rights (a logon scheduled task needs elevation).
# It keeps running while the user is logged in; a disconnected RDP session is
# fine, a logged-out one is not.
$runCmd = Join-Path $RunnerDir "run.cmd"
$startup = [Environment]::GetFolderPath('Startup')
$lnk = Join-Path $startup ($TaskName + ".lnk")
Write-Host "Creating Startup shortcut '$lnk'..."
$ws = New-Object -ComObject WScript.Shell
$sc = $ws.CreateShortcut($lnk)
$sc.TargetPath = $runCmd
$sc.WorkingDirectory = $RunnerDir
$sc.WindowStyle = 7   # minimised
$sc.Description = "GitHub Actions self-hosted runner for the Nutshell integration tests"
$sc.Save()
Write-Host "Starting the runner now..."
Start-Process -FilePath $runCmd -WorkingDirectory $RunnerDir -WindowStyle Minimized | Out-Null

Write-Host ""
Write-Host "Runner registered. Check it under https://github.com/$Repo/settings/actions/runners"
Write-Host "It must show as Idle (green) before the integration tests can run."
