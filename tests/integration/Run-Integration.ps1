# Run-Integration.ps1 — end-to-end tests of nutshell.exe against a live SSH host.
#
# Usage (Windows PowerShell 5.1):
#   .\tests\integration\Run-Integration.ps1 -HostName tompi -User thomas -KeyPath $HOME\.ssh\thomas
#
# Prerequisites: build\win\nutshell.exe built from the current tree; the key
# authorised on the host; nothing else grabbing keyboard focus while it runs
# (keystrokes are delivered to the foreground window). Screenshots and logs
# land in tests\integration\artifacts\.

param(
    [string] $HostName = "tompi",
    [string] $User = $env:USERNAME,
    [string] $KeyPath = (Join-Path $HOME ".ssh\thomas"),
    [string] $Exe = (Join-Path $PSScriptRoot "..\..\build\win\nutshell.exe"),
    [string[]] $Only = @(),
    # AI Assist cases: provider/model used with the key from NUTSHELL_IT_AI_KEY or tests\integration\.ai_key
    [string] $AiProvider = "moonshot",
    [string] $AiModel = "kimi-k3"
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "NutshellIT.psm1") -Force
$Artifacts = Join-Path $PSScriptRoot "artifacts"
New-Item -ItemType Directory -Force $Artifacts | Out-Null
$Exe = (Resolve-Path $Exe).Path

$results = New-Object System.Collections.ArrayList

function Invoke-Case {
    param([string] $Name, [hashtable] $Settings, [scriptblock] $Body)
    if ($Only.Count -gt 0 -and $Only -notcontains $Name) { return }
    Write-Host ("[RUN ] " + $Name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath -Settings $Settings
    $session = $null
    $ok = $false; $detail = ""
    try {
        $session = Start-Nutshell -Env $testEnv
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
        Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
    }
    if ($ok) { Write-Host ("[PASS] " + $Name) } else { Write-Host ("[FAIL] " + $Name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $Name; Passed = $ok; Detail = $detail })
}

function Assert-True { param([bool] $Cond, [string] $Message) if (-not $Cond) { throw $Message } }

# ---- Cases --------------------------------------------------------------------

Invoke-Case "connect_shows_prompt" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line "echo CONNECTED_MARKER"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "CONNECTED_MARKER" -TimeoutSec 10) "no shell output reached the session log"
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "connect_shows_prompt.png") | Out-Null
    "prompt reached via key auth"
}

Invoke-Case "ctrl_c_without_selection_interrupts" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line "sleep 30"
    Start-Sleep -Milliseconds 800
    Send-NutshellKeys -Session $s -Keys "^c" -SettleMs 500
    Send-NutshellLine -Session $s -Line "echo AFTER_$((1+1))"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "AFTER_2" -TimeoutSec 5) "shell did not return within 5s: Ctrl+C was not delivered as SIGINT"
    "sleep 30 interrupted; prompt returned"
}

Invoke-Case "log_filename_follows_log_format" @{ log_format = "%Y%m%d-%H%M" } {
    param($s)
    $log = Start-NutshellLogging -Session $s
    Wait-NutshellShell -Session $s
    $leaf = Split-Path $log -Leaf
    Assert-True ($leaf -match '^\d{8}-\d{4}_it\.log$') "log name '$leaf' does not follow <fmt>_<name>.log"
    "log file: $leaf"
}

Invoke-Case "paste_without_confirmation" @{ paste_confirm = $false } {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Set-Clipboard -Value "echo PASTE_OK_42"
    Send-NutshellKeys -Session $s -Keys "^v" -SettleMs 600
    $wins = Get-NutshellWindows -Session $s
    Assert-True (-not ($wins | Where-Object { $_ -match "Paste" })) "a paste confirmation window appeared although paste_confirm is off"
    Send-NutshellKeys -Session $s -Keys "{ENTER}"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "PASTE_OK_42" -TimeoutSec 5) "pasted text never reached the shell"
    "pasted straight through"
}

Invoke-Case "paste_with_confirmation_shows_dialog" @{ paste_confirm = $true } {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Set-Clipboard -Value "echo PASTE_CONFIRM_7"
    Send-NutshellKeys -Session $s -Keys "^v" -SettleMs 800
    $dlg = Get-NutshellWindows -Session $s | Where-Object { $_ -notmatch "Nutshell_Window" } | Select-Object -First 1
    Assert-True ($null -ne $dlg) "no confirmation window appeared with paste_confirm on"
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "paste_confirm_dialog.png") -Hwnd ([long]($dlg -split "`t")[0]) | Out-Null
    Send-NutshellKeys -Session $s -Keys "{ESC}" -SettleMs 400
    "dialog shown: " + ($dlg -split "`t")[2]
}

Invoke-Case "pty_resizes_with_window" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Set-NutshellWindowSize -Session $s -Width 1600 -Height 1000
    Send-NutshellLine -Session $s -Line 'echo SIZE_A=$(tput lines)x$(tput cols)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "SIZE_A=(\d+)x(\d+)" -TimeoutSec 5) "no size report at first size"
    Set-NutshellWindowSize -Session $s -Width 1000 -Height 600
    Send-NutshellLine -Session $s -Line 'echo SIZE_B=$(tput lines)x$(tput cols)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "SIZE_B=(\d+)x(\d+)" -TimeoutSec 5) "no size report at second size"
    $t = Get-NutshellLogText -Session $s
    $a = [regex]::Match($t, "SIZE_A=(\d+)x(\d+)"); $b = [regex]::Match($t, "SIZE_B=(\d+)x(\d+)")
    Assert-True ([int]$b.Groups[1].Value -lt [int]$a.Groups[1].Value) "rows did not shrink: $($a.Value) -> $($b.Value)"
    Assert-True ([int]$b.Groups[2].Value -lt [int]$a.Groups[2].Value) "cols did not shrink: $($a.Value) -> $($b.Value)"
    "$($a.Value) -> $($b.Value)"
}

Invoke-Case "page_up_scrolls_history" @{} {
    param($s)
    # No programmatic read of the screen exists yet; this case produces the
    # evidence screenshots (before/after Page Up) for eyeballing.
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line "seq 1 300"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^300\s*$" -TimeoutSec 5) "seq output incomplete"
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "page_up_before.png") | Out-Null
    Send-NutshellKeys -Session $s -Keys "{PGUP}" -SettleMs 400
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "page_up_after.png") | Out-Null
    "screenshots saved (visual check)"
}

Invoke-Case "resize_applies_to_inactive_tab" @{} {
    param($s)
    # Regression for "lost lines after resize": WM_SIZE only resized the active
    # tab, so a background tab kept its old grid and PTY size until the next
    # resize. Resize on tab B, switch to tab A, and A must report the new size.
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Set-NutshellWindowSize -Session $s -Width 1200 -Height 700
    Send-NutshellLine -Session $s -Line 'echo TAB_A_READY'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "TAB_A_READY" -TimeoutSec 5) "tab A not ready"
    Open-NutshellSecondTab -Session $s
    Assert-True (((Get-NutshellWindows -Session $s) | Where-Object { $_ -match "Session Manager" }).Count -eq 0) "Session Manager still open; Connect failed"
    Set-NutshellWindowSize -Session $s -Width 1600 -Height 1300
    Select-NutshellTab -Session $s -Index 0
    Send-NutshellLine -Session $s -Line 'echo A_LINES=$(tput lines)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "A_LINES=(\d+)" -TimeoutSec 5) "no size report from tab A"
    $a = [int][regex]::Match((Get-NutshellLogText -Session $s), "A_LINES=(\d+)").Groups[1].Value
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "inactive_tab_after_resize.png") | Out-Null
    Assert-True ($a -ge 30) "tab A still has the pre-resize grid: tput lines = $a (expected >= 30 for a 1300px-tall window)"
    "tab A reports $a lines after the resize happened on tab B"
}

# ---- AI Assist cases ----------------------------------------------------------
# Skipped (not failed) when no key is available. Kept deliberately cheap: two
# real requests, a 30-line terminal context, no web tools. Assertions read the
# terminal log, so they prove the whole loop: prompt -> reply -> [EXEC] parse ->
# approval -> execution over SSH.

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
    if ($Only.Count -gt 0 -and $Only -notcontains $Name) { return }
    if (-not $AiKey) {
        Write-Host ("[SKIP] " + $Name + " -- no key: set NUTSHELL_IT_AI_KEY or create tests\integration\.ai_key")
        return
    }
    Invoke-Case $Name $AiSettings $Body
}

Invoke-AiCase "ai_panel_docks_with_key" {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    $p = Open-NutshellAiPanel -Session $s
    $dialogs = @((Get-NutshellWindows -Session $s) | Where-Object { $_ -match "`t#32770`t" })
    Assert-True ($dialogs.Count -eq 0) "a dialog appeared when opening the panel: $($dialogs -join '; ')"
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "ai_panel_docked.png") | Out-Null
    "panel docked (hwnd $p)"
}

Invoke-AiCase "ai_runs_safe_command_with_auto_approve" {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Open-NutshellAiPanel -Session $s | Out-Null
    Set-NutshellAiAutoApprove -Session $s
    $marker = "AI_PONG_" + (Get-Random -Minimum 100 -Maximum 999)
    Send-NutshellAiPrompt -Session $s -Text "Run exactly this shell command and nothing else, no explanation: echo $marker"
    $ok = Wait-NutshellLog -Session $s -Pattern ("(?m)^" + $marker + "\s*$") -TimeoutSec 90
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "ai_safe_command.png") | Out-Null
    Assert-True $ok "the AI's echo never ran in the terminal within 90s (see ai_safe_command.png for the reply)"
    "AI ran echo $marker via auto-approve"
}

Invoke-AiCase "ai_write_command_blocked_without_permit_write" {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Open-NutshellAiPanel -Session $s | Out-Null
    Set-NutshellAiAutoApprove -Session $s          # auto-approve on, Permit Write still off
    $file = "/tmp/nutshell_it_blocked_" + (Get-Random -Minimum 100 -Maximum 999)
    Send-NutshellAiPrompt -Session $s -Text "Run exactly this shell command and nothing else, no explanation: touch $file"
    Start-Sleep -Seconds 45
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "ai_blocked_command.png") | Out-Null
    $ran = (Get-NutshellLogText -Session $s) -match [regex]::Escape("touch $file")
    Assert-True (-not $ran) "a write command reached the terminal although Permit Write is off"
    Set-NutshellTerminalFocus -Session $s   # the panel took keyboard focus; the check must go to the shell
    Send-NutshellLine -Session $s -Line "test -e $file && echo BLOCK_FAIL || echo BLOCK_OK"
    # Anchor to a line start: the echoed command line itself contains both words.
    Assert-True (Wait-NutshellLog -Session $s -Pattern '(?m)^BLOCK_(OK|FAIL)\s*$' -TimeoutSec 8) "the existence check never reached the shell"
    Assert-True ((Get-NutshellLogText -Session $s) -notmatch '(?m)^BLOCK_FAIL\s*$') "the file exists: the write command was executed"
    "write command held back; $file not created"
}

# ---- Summary ------------------------------------------------------------------

$passed = @($results | Where-Object { $_.Passed }).Count
$failed = @($results | Where-Object { -not $_.Passed }).Count
Write-Host ""
Write-Host ("Integration: {0} passed, {1} failed" -f $passed, $failed)
$results | Format-Table -AutoSize | Out-String | Write-Host
if ($failed -gt 0) { exit 1 } else { exit 0 }
