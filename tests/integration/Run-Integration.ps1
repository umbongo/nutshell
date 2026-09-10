# Run-Integration.ps1 -- end-to-end tests of nutshell.exe against a live SSH host.
#
# Usage (Windows PowerShell 5.1):
#   .\tests\integration\Run-Integration.ps1 -HostName tompi -User thomas -KeyPath $HOME\.ssh\thomas
#
# Prerequisites: build\win\nutshell.exe built from the current tree; the key
# authorised on the host. Typing and dialog-driving go through NutshellIT.psm1's
# posted-message helpers (Send-NutshellText/-Key/-Line, Wait-NutshellDialog,
# Get/Set-NutshellControl*, ...); the modifier chords the app reads via
# GetKeyState (Ctrl+C/V, Shift+Insert, Ctrl+= zoom) go through
# Send-NutshellChord, which borrows the app's own keyboard state with
# AttachThreadInput. No case needs the foreground window, keyboard focus or an
# unlocked desktop -- the whole suite runs on a locked or RDP-disconnected
# session. Screenshots and logs land in tests\integration\artifacts\.
#
# Tiers (-Tier gate|ai|nightly|all, default all): gate is the merge gate --
# every non-AI case plus
# ui_gallery/approval_card_run_selected_settles/ai_panel_opens_without_key (no
# AI key needed despite the ai_ prefix); ai is the five key-gated ai_* cases;
# nightly is currently empty (reserved for slow/flaky cases, e.g. idle-timeout
# or host-unreachable scenarios, per
# docs/superpowers/specs/2026-09-09-integration-coverage.md's
# proposed tiers -- none of those cases exist yet, only the tier plumbing does).
#
# ---- Layout -----------------------------------------------------------------
# This script is the driver only: params, Invoke-Case/Invoke-AiCase,
# Assert-True, Invoke-KnownBugBlock, the shared capture helpers used by more
# than one case file (Test-NutshellCaptureNonBlank, ConvertTo-NutshellFileToken
# -- both defined below in this file, so every dot-sourced case file can see
# them), the results table and the exit code. Helpers needed by only one case
# file live in that file instead (e.g. Get-TerminalAreaHash in
# cases\10-terminal.ps1). Case bodies live in
# tests\integration\cases\*.ps1, dot-sourced below in name order so they run
# in this script's own scope (Invoke-Case, $Artifacts, $results, etc. are all
# visible to them without passing anything explicitly):
#   10-terminal.ps1     connect, ctrl_c, log filename, paste x2, pty resize,
#                        page up, holds position, inactive tab resize
#   20-ai.ps1            the five key-gated ai_* cases + ai_panel_opens_without_key
#   30-ui-demo.ps1       ui_gallery, approval_card_run_selected_settles,
#                        helpers_theme_pixel_matches_token
#   40-helpers.ps1       the other helpers_* cases
#   50-window.ps1        launch/window/shutdown/menu (LAUNCH-*, RESIZE-1,
#                        WINDOW-*, CLOSE-1, MENU-1)
#   60-tabs-logging.ps1  tabs (TABS-*) and logging (LOG-*)
#   70-cli.ps1           CLI flags (CLI-1's several small cases)
# Add a new case by editing the matching file (or adding a new cases\NN-*.ps1
# -- any file matching cases\*.ps1 is picked up automatically) rather than
# growing this file.

param(
    [string] $HostName = "tompi",
    [string] $User = $env:USERNAME,
    [string] $KeyPath = (Join-Path $HOME ".ssh\thomas"),
    [string] $Exe = (Join-Path $PSScriptRoot "..\..\build\win\nutshell.exe"),
    [string[]] $Only = @(),
    [ValidateSet("gate", "ai", "nightly", "all")]
    [string] $Tier = "all",
    # AI Assist cases: provider/model used with the key from NUTSHELL_IT_AI_KEY or tests\integration\.ai_key
    [string] $AiProvider = "moonshot",
    [string] $AiModel = "kimi-k3"
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "NutshellIT.psm1") -Force
$Artifacts = Join-Path $PSScriptRoot "artifacts"
New-Item -ItemType Directory -Force $Artifacts | Out-Null
$Exe = (Resolve-Path $Exe).Path

$ActiveTiers = if ($Tier -eq "all") { @("gate", "ai", "nightly") } else { @($Tier) }
Write-Host ("Tier(s) requested: " + ($ActiveTiers -join ", "))

# -Exe validation: confirm the binary under test actually runs and report its
# version, so a stale build\win\nutshell.exe left over from an earlier
# checkout doesn't silently run against the wrong code.
$exeVersion = Get-NutshellExeVersion -Exe $Exe
if ($exeVersion) {
    Write-Host ("Exe: " + $Exe + " (version " + $exeVersion + ")")
} else {
    Write-Host ("Exe: " + $Exe + " (version could not be captured -- see Get-NutshellExeVersion's doc comment; not fatal)")
}

$results = New-Object System.Collections.ArrayList

# WM_COMMAND/WM_CLOSE/WM_CHAR are module-private constants ($script:WM_* inside
# NutshellIT.psm1, not exported); case files that need to post one directly
# (rather than through a helper like Send-NutshellCommand) use these.
$WM_COMMAND = 0x0111
$WM_CLOSE   = 0x0010
$WM_CHAR    = 0x0102

function Invoke-Case {
    <# -ExtraArgs, when given, launches with these args verbatim instead of
       "-sn <profile>" (e.g. @("-h", "tompi") or @("-nc")) -- see Start-Nutshell's
       own -ExtraArgs doc comment. -NoConfig skips writing nutshell.config
       (LAUNCH-2/LAUNCH-3's first-run/corrupt-config scenarios); the case body
       is then responsible for whatever it wants at $s.Env.Root\nutshell.config
       (nothing, or its own file) before/while the process is starting -- but
       since New-NutshellTestEnv must return before Start-Nutshell runs, a
       case needing a *pre-existing corrupt* file can't use Invoke-Case at all
       (see LAUNCH-3's standalone block in 50-window.ps1) and instead calls
       New-NutshellTestEnv/Start-Nutshell itself. #>
    param([string] $Name, [hashtable] $Settings, [scriptblock] $Body, [string] $CaseTier = "gate",
          [string[]] $ExtraArgs = @(), [switch] $NoConfig)
    if ($ActiveTiers -notcontains $CaseTier) { return }
    if ($Only.Count -gt 0 -and $Only -notcontains $Name) { return }
    Write-Host ("[RUN ] " + $Name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath -Settings $Settings -NoConfig:$NoConfig
    $session = $null
    $ok = $false; $detail = ""
    try {
        $session = if ($ExtraArgs.Count -gt 0) { Start-Nutshell -Env $testEnv -ExtraArgs $ExtraArgs } else { Start-Nutshell -Env $testEnv }
        $detail = & $Body $session
        $ok = $true
    } catch {
        $detail = $_.Exception.Message
        if ($session) { try { Save-NutshellScreenshot -Session $session -Path (Join-Path $Artifacts "$Name-FAIL.png") | Out-Null } catch {} }
    } finally {
        if ($session) { Stop-Nutshell -Session $session }
        if ($session -and $session.Log -and (Test-Path $session.Log)) {
            Copy-Item $session.Log (Join-Path $Artifacts "$Name.log") -Force
        }
        for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
            Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
            if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
        }
    }
    if ($ok) { Write-Host ("[PASS] " + $Name) } else { Write-Host ("[FAIL] " + $Name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $Name; Passed = $ok; Detail = $detail })
}

function Assert-True { param([bool] $Cond, [string] $Message) if (-not $Cond) { throw $Message } }

function Invoke-KnownBugBlock {
    <# Run assertions that are known to fail today because of a named *product*
       bug (not a harness bug), without failing the tier -- so the gate
       stays a signal about regressions rather than about a bug we have already
       written down and scheduled. The block runs for real; whichever way it
       goes is reported in the case's detail column, so a fix shows up as
       "unexpectedly PASSED" rather than silently rotting.

       Everything a case can still assert unconditionally must stay outside the
       block -- wrap only the checks the named bug actually breaks.

       Returns a detail fragment for the case to append to its own detail
       string; -Bug is a short description of the product bug (what is broken
       and where), so the summary table names it. #>
    param([Parameter(Mandatory)] [string] $Bug, [Parameter(Mandatory)] [scriptblock] $Body)
    try {
        & $Body | Out-Null
        Write-Host ("[XPASS] known bug '" + $Bug + "' -- its check passed; if it is fixed, unwrap the Invoke-KnownBugBlock")
        return "KNOWN BUG [$Bug]: its check unexpectedly PASSED -- looks fixed, unwrap the known-bug block"
    } catch {
        Write-Host ("[XFAIL] known bug '" + $Bug + "' -- " + $_.Exception.Message)
        return "KNOWN BUG [$Bug]: still failing as expected -- $($_.Exception.Message)"
    }
}

function Test-NutshellCaptureNonBlank {
    <# A handful of sampled pixels must not all be identical -- proof the
       window actually painted, not just that PrintWindow returned bits.
       Used by several case files (20-ai.ps1, 30-ui-demo.ps1, 50-window.ps1),
       so it lives in the driver rather than any one of them. #>
    param([Parameter(Mandatory)] [string] $Path)
    $bmp = New-Object System.Drawing.Bitmap $Path
    try {
        $w = $bmp.Width; $h = $bmp.Height
        if ($w -le 0 -or $h -le 0) { return $false }
        $xs = @(2, [int]($w / 4), [int]($w / 2), [int](3 * $w / 4), ($w - 3)) |
            Where-Object { $_ -ge 0 -and $_ -lt $w } | Select-Object -Unique
        $ys = @(2, [int]($h / 4), [int]($h / 2), [int](3 * $h / 4), ($h - 3)) |
            Where-Object { $_ -ge 0 -and $_ -lt $h } | Select-Object -Unique
        $colors = New-Object System.Collections.Generic.HashSet[int]
        foreach ($x in $xs) { foreach ($y in $ys) { [void]$colors.Add($bmp.GetPixel($x, $y).ToArgb()) } }
        return $colors.Count -gt 1
    } finally {
        $bmp.Dispose()
    }
}

function ConvertTo-NutshellFileToken {
    <# Sanitise a display name for use in a filename: spaces -> '-', '&' -> 'and'.
       Used by 30-ui-demo.ps1's ui_gallery. #>
    param([Parameter(Mandatory)] [string] $Text)
    return ($Text -replace '\s+', '-') -replace '&', 'and'
}

# ---- AI Assist setup ------------------------------------------------------------
# Skipped (not failed) when no key is available. Kept deliberately cheap: two
# real requests, a 30-line terminal context, no web tools. Assertions read the
# terminal log, so they prove the whole loop: prompt -> reply -> [EXEC] parse ->
# approval -> execution over SSH. Used by cases\20-ai.ps1.

$AiCfg = Get-NutshellAiConfig
$AiKey = $null
if ($AiCfg) {
    $AiKey = $AiCfg.Key
    # A saved .ai_config wins over the script defaults unless overridden on the command line.
    if ($AiCfg.Provider -and -not $PSBoundParameters.ContainsKey("AiProvider")) { $AiProvider = $AiCfg.Provider }
    if ($AiCfg.Model -and -not $PSBoundParameters.ContainsKey("AiModel")) { $AiModel = $AiCfg.Model }
}
$AiSettings = @{ ai_provider = $AiProvider; ai_custom_model = $AiModel; ai_api_key = $AiKey
                 ai_max_context_lines = 30; ai_search_provider = "none"; ai_web_fetch_enabled = $false }

function Invoke-AiCase {
    param([string] $Name, [scriptblock] $Body)
    if ($ActiveTiers -notcontains "ai") { return }
    if ($Only.Count -gt 0 -and $Only -notcontains $Name) { return }
    if (-not $AiKey) {
        Write-Host ("[SKIP] " + $Name + " -- no key: set NUTSHELL_IT_AI_KEY or create tests\integration\.ai_key")
        return
    }
    Invoke-Case $Name $AiSettings $Body "ai"
}

# ---- Cases (tests\integration\cases\*.ps1, dot-sourced in name order) -----------

Get-ChildItem -Path (Join-Path $PSScriptRoot "cases") -Filter "*.ps1" | Sort-Object Name | ForEach-Object {
    . $_.FullName
}

# ---- Summary ------------------------------------------------------------------

$passed = @($results | Where-Object { $_.Passed }).Count
$failed = @($results | Where-Object { -not $_.Passed }).Count
Write-Host ""
Write-Host ("Integration: {0} passed, {1} failed" -f $passed, $failed)
$results | Format-Table -AutoSize | Out-String | Write-Host
if ($failed -gt 0) { exit 1 } else { exit 0 }
