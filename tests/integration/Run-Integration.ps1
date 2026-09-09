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

# WM_COMMAND itself is a module-private constant ($script:WM_COMMAND inside
# NutshellIT.psm1, not exported); cases in this script that need to post one
# directly (rather than through a helper like Send-NutshellCommand) use this.
$WM_COMMAND = 0x0111

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
        for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
            Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
            if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
        }
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

function Get-TerminalAreaHash {
    <# MD5 of the terminal text area of a main-window capture: the left 60% of
       the width (excludes the scrollbar) between 15% and 95% of the height
       (excludes the title bar / tab strip). While scrolled back the cursor is
       not drawn, so two captures of an unchanged view hash identically. #>
    param([Parameter(Mandatory)] [string] $Path)
    $bmp = New-Object System.Drawing.Bitmap $Path
    try {
        $rect = New-Object System.Drawing.Rectangle -ArgumentList @(0, [int]($bmp.Height * 0.15), [int]($bmp.Width * 0.6), [int]($bmp.Height * 0.8))
        $crop = $bmp.Clone($rect, $bmp.PixelFormat)
        try {
            $ms = New-Object System.IO.MemoryStream
            $crop.Save($ms, [System.Drawing.Imaging.ImageFormat]::Bmp)
            $md5 = [System.Security.Cryptography.MD5]::Create()
            return [BitConverter]::ToString($md5.ComputeHash($ms.ToArray()))
        } finally { $crop.Dispose() }
    } finally { $bmp.Dispose() }
}

Invoke-Case "terminal_holds_position_while_output_arrives" @{} {
    param($s)
    # Smart scrolling (v1.1.0): a view scrolled back into history must stay on
    # the same lines while new output arrives, and a keypress returns to the
    # live view. Before the fix the offset was measured from the bottom and
    # never adjusted, so every new line dragged the view along.
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line "seq 1 300"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^300\s*$" -TimeoutSec 5) "seq output incomplete"
    # Output that arrives with no keypress: 101 lines after a 4 s delay.
    Send-NutshellLine -Session $s -Line "(sleep 4; seq 1000 1100) &"
    Send-NutshellKeys -Session $s -Keys "{PGUP}{PGUP}" -SettleMs 500
    $before = Join-Path $Artifacts "smart_scroll_before.png"
    $after  = Join-Path $Artifacts "smart_scroll_after.png"
    $live   = Join-Path $Artifacts "smart_scroll_live.png"
    Save-NutshellScreenshot -Session $s -Path $before | Out-Null
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^1100\s*$" -TimeoutSec 12) "the background output never arrived"
    Start-Sleep -Milliseconds 600
    Save-NutshellScreenshot -Session $s -Path $after | Out-Null
    Assert-True ((Get-TerminalAreaHash $before) -eq (Get-TerminalAreaHash $after)) "the scrolled-back view moved when new output arrived (see smart_scroll_before/after.png)"
    Send-NutshellKeys -Session $s -Keys "{ENTER}" -SettleMs 600
    Save-NutshellScreenshot -Session $s -Path $live | Out-Null
    Assert-True ((Get-TerminalAreaHash $after) -ne (Get-TerminalAreaHash $live)) "a keypress did not return to the live view"
    "view held while 101 lines arrived; Enter returned to the live view"
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

Invoke-AiCase "ai_commands_run_one_at_a_time" {
    param($s)
    # Prompt-gated command dispatch (2026-09-07-command-dispatch-and-auto-approve-levels.md,
    # section A): commands must be sent one at a time, each only once the
    # terminal is back at a shell prompt. Without that gating the tty echoes
    # the second command's typed text immediately -- while "sleep 6" is
    # still running -- so its echo would land in the log before FIRST_DONE's
    # output. sleep 6 gives that race a real window to lose in.
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Open-NutshellAiPanel -Session $s | Out-Null
    Set-NutshellAiAutoApprove -Session $s
    Send-NutshellAiPrompt -Session $s -Text ("Run these two commands as two separate EXEC blocks, in this order, " +
        "nothing else and no explanation: sleep 6 && echo FIRST_DONE ; then: echo SECOND_DONE")
    $ok = Wait-NutshellLog -Session $s -Pattern '(?m)^SECOND_DONE\s*$' -TimeoutSec 90
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "ai_commands_sequential.png") | Out-Null
    Assert-True $ok "SECOND_DONE never appeared in the terminal within 90s"

    $log = Get-NutshellLogText -Session $s
    Assert-True ($log -match '(?m)^FIRST_DONE\s*$') "the output line FIRST_DONE never appeared in the log"

    # Anchor FIRST_DONE to its output line (not the earlier echoed command
    # text "... && echo FIRST_DONE"), and compare against where the second
    # command's echoed text shows up.
    $mFirst = [regex]::Match($log, '(?m)^FIRST_DONE\s*$')
    $idxEcho = $log.IndexOf("echo SECOND_DONE")
    Assert-True ($idxEcho -ge 0) "the echoed text 'echo SECOND_DONE' never appeared in the log"
    Assert-True ($idxEcho -gt $mFirst.Index) ("echo SECOND_DONE was typed into the terminal (index $idxEcho) before " +
        "FIRST_DONE's output line (index $($mFirst.Index)) -- commands were not gated on the shell prompt")
    "commands ran one at a time: FIRST_DONE (index $($mFirst.Index)) before echo SECOND_DONE was typed (index $idxEcho)"
}

Invoke-AiCase "ai_write_command_held_then_runs_after_permit" {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    $p = Open-NutshellAiPanel -Session $s
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

    # Bug 2 regression: switch to Read + write (IDC_CHAT_PERMIT) and run the
    # held command via "Run N selected" (IDC_CMD_APPROVE_SEL) -- before the
    # fix, the write-only batch was never queued (only safe commands were),
    # so queued_count stayed 0 and nothing ran even after unblocking.
    [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]4005, [IntPtr]::Zero) | Out-Null   # IDC_CHAT_PERMIT
    Start-Sleep -Seconds 1
    [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]3045, [IntPtr]::Zero) | Out-Null   # IDC_CMD_APPROVE_SEL
    Assert-True (Wait-NutshellLog -Session $s -Pattern ([regex]::Escape("touch $file")) -TimeoutSec 10) "the held command never reached the terminal after switching to Read + write and running it"

    # Running the card moved the caret to the panel's input box; put the
    # keyboard back in the terminal before typing the check.
    Set-NutshellTerminalFocus -Session $s
    Send-NutshellLine -Session $s -Line "test -e $file && echo RAN_OK || echo RAN_FAIL"
    Assert-True (Wait-NutshellLog -Session $s -Pattern '(?m)^RAN_(OK|FAIL)\s*$' -TimeoutSec 8) "the post-run existence check never reached the shell"
    Assert-True ((Get-NutshellLogText -Session $s) -match '(?m)^RAN_OK\s*$') "the file still does not exist: the held command was not actually run"
    "write command held back, then ran after Permit Write + Run selected; $file created"
}

Invoke-AiCase "ai_prompt_while_approval_pending" {
    param($s)
    # Pending command batches (docs/superpowers/specs/
    # 2026-09-09-pending-command-batches.md, rule 1): a card must never
    # block the input. Auto-approve stays off (the default) so the first
    # reply's command lands in a pending card; a second prompt must still
    # get a normal reply while that card sits there, and the card must
    # still be actionable afterwards.
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    $p = Open-NutshellAiPanel -Session $s

    $marker = "BATCH_A_" + (Get-Random -Minimum 100 -Maximum 999)
    Send-NutshellAiPrompt -Session $s -Text ("Run exactly this shell command and nothing else, no explanation: echo " + $marker)
    Wait-NutshellAiSendIdle -Session $s   # first reply finished -- its card is now pending
    Assert-True ((Get-NutshellLogText -Session $s) -notmatch [regex]::Escape($marker)) "the command ran before it was approved"

    Send-NutshellAiPrompt -Session $s -Text "Reply with exactly the word PONG and no commands"
    Wait-NutshellAiSendIdle -Session $s   # second reply finished -- proves the pending card never blocked sending

    [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]3045, [IntPtr]::Zero) | Out-Null   # IDC_CMD_APPROVE_SEL, lParam 0 = oldest pending batch
    Assert-True (Wait-NutshellLog -Session $s -Pattern ("(?m)^" + $marker + "\s*$") -TimeoutSec 15) "the first batch's command never ran after Run selected on lParam 0"
    "second prompt got a reply while the first batch's card was pending; Run selected (lParam 0) then ran it: $marker"
}

# ---- UI gallery ----------------------------------------------------------------
# Contact sheet of every --ui-demo state x theme (Design-System Foundation,
# spec section 5; AI Assist Panel plan task 5 added "nokey"/"nosession").
# Needs no SSH host or key -- --ui-demo never connects -- so this runs as its
# own block, gated on $Only the same way Invoke-Case is, rather than through
# it: it must not call Wait-NutshellShell or Start-NutshellLogging, both of
# which assume a live shell prompt.
#
# $galleryStates is a hand-kept copy of src/core/ui_demo.c's STATE_NAMES --
# there is no cheap way for a PowerShell script to query the C array at
# build time, and --ui-demo=<unknown>'s error text ("Unknown demo state:
# <name>") does not enumerate the valid ones. Native coverage that this
# list can't silently drift from ui_demo_states() lives in
# tests/test_ui_demo.c (test_ui_demo_states_lists_ten_ending_in_all et al.)
# -- keep both lists in sync by hand when a state is added/removed.
# "batches" (docs/superpowers/specs/2026-09-09-pending-command-batches.md)
# is the one state whose whole point is two independent pending cards at
# once -- its capture is the visual proof that both render side by side.

function Test-NutshellCaptureNonBlank {
    <# A handful of sampled pixels must not all be identical -- proof the
       window actually painted, not just that PrintWindow returned bits. #>
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
    <# Sanitise a display name for use in a filename: spaces -> '-', '&' -> 'and'. #>
    param([Parameter(Mandatory)] [string] $Text)
    return ($Text -replace '\s+', '-') -replace '&', 'and'
}

if ($Only.Count -eq 0 -or $Only -contains "ui_gallery") {
    Write-Host ("[RUN ] ui_gallery")
    $galleryDir = Join-Path $Artifacts "gallery"
    New-Item -ItemType Directory -Force $galleryDir | Out-Null
    $galleryStates = @("chat", "approval", "executing", "tool", "error", "empty", "nokey", "nosession", "batches", "all")
    $galleryThemes = @("Onyx Synapse", "Onyx Light", "Sage & Sand", "Moss & Mist")
    $galleryOk = $true
    $galleryDetail = New-Object System.Collections.ArrayList
    foreach ($theme in $galleryThemes) {
        foreach ($state in $galleryStates) {
            $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
            $session = $null
            try {
                $session = Start-Nutshell -Env $testEnv -ExtraArgs @("--ui-demo=$state", "--theme", $theme)
                Set-NutshellWindowSize -Session $session -Width 1400 -Height 900
                $fileName = "{0}-{1}.png" -f (ConvertTo-NutshellFileToken $theme), $state
                $path = Join-Path $galleryDir $fileName
                Save-NutshellScreenshot -Session $session -Path $path | Out-Null
                if (-not (Test-NutshellCaptureNonBlank -Path $path)) {
                    throw "capture looks blank: $fileName"
                }
            } catch {
                $galleryOk = $false
                [void]$galleryDetail.Add("$theme/${state}: " + $_.Exception.Message)
            } finally {
                if ($session) { Stop-Nutshell -Session $session }
                for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
                    Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
                    if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
                }
            }
        }
    }
    $galleryReport = $galleryDetail -join "; "
    if ($galleryOk) { Write-Host "[PASS] ui_gallery" } else { Write-Host ("[FAIL] ui_gallery -- " + $galleryReport) }
    [void]$results.Add([pscustomobject]@{ Name = "ui_gallery"; Passed = $galleryOk; Detail = $galleryReport })
}

# ---- Approval card: Run N selected settles without crashing --------------------
# Regression for the v1.0.97 crash fix: build_cmd_card_geometry derived its row
# count from the stale lv->cmd_count left over from before the last settle
# instead of the live walk, so WM_PAINT (which never recalc_layouts) could paint
# a command row against a NULL cmd_items[] slot. --ui-demo=approval seeds a
# live container with one PENDING and one BLOCKED command; IDC_CMD_APPROVE_SEL
# ("Run N selected") drives the exact settle-then-repaint path that crashed.
# Needs no SSH host or AI key -- --ui-demo never connects -- so this runs as
# its own block like ui_gallery above, not through Invoke-Case/Invoke-AiCase
# (both assume a live shell prompt).
if ($Only.Count -eq 0 -or $Only -contains "approval_card_run_selected_settles") {
    $name = "approval_card_run_selected_settles"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
    $session = $null
    $ok = $false; $detail = ""
    try {
        $session = Start-Nutshell -Env $testEnv -ExtraArgs @("--ui-demo=approval")
        Start-Sleep -Seconds 2
        $p = Get-NutshellAiPanel -Session $session
        if ($p -eq [IntPtr]::Zero) { throw "AI Assist panel did not open in --ui-demo=approval" }
        [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]3045, [IntPtr]::Zero) | Out-Null  # IDC_CMD_APPROVE_SEL
        # Poll rather than a single sleep-then-check: right after an access
        # violation the process can sit in WER's crash-handling dialog for a
        # moment with HasExited still False (window present but frozen), so
        # a lone HasExited check after one sleep can read as "alive" even
        # though the app already crashed -- Responding (a live SendMessage
        # ping) flips False as soon as the UI thread stops pumping messages,
        # well before the process actually terminates.
        $crashed = $false
        for ($i = 0; $i -lt 10; $i++) {
            Start-Sleep -Milliseconds 500
            $session.Process.Refresh()
            if ($session.Process.HasExited -or -not $session.Process.Responding) { $crashed = $true; break }
        }
        Assert-True (-not $crashed) "nutshell.exe crashed (or stopped responding) after Run N selected on the approval card"
        $path = Join-Path $Artifacts "$name.png"
        Save-NutshellScreenshot -Session $session -Path $path | Out-Null
        Assert-True (Test-NutshellCaptureNonBlank -Path $path) "capture looks blank: $name.png"
        $detail = "process survived Run N selected on the approval card; capture non-blank"
        $ok = $true
    } catch {
        $detail = $_.Exception.Message
        if ($session) { try { Save-NutshellScreenshot -Session $session -Path (Join-Path $Artifacts "$name-FAIL.png") | Out-Null } catch {} }
    } finally {
        if ($session) { Stop-Nutshell -Session $session }
        for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
            Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
            if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
        }
    }
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}

# ---- AI panel without a key -----------------------------------------------------
# Keystroke-free: unlike Invoke-AiCase (which needs a real API key to send a
# prompt), this only needs a connected session and an *empty* ai_api_key --
# proof that the two MessageBox dead ends (AI Assist Panel design, "Empty and
# blocked states") are gone and the panel opens straight into the no-key
# state instead. Runs through Invoke-Case directly (not Invoke-AiCase) since
# it must run even when no AI key is configured for the other ai_* cases.
Invoke-Case "ai_panel_opens_without_key" @{ ai_api_key = "" } {
    param($s)
    Send-NutshellCommand -Session $s -Id 2020 -SettleMs 1500      # IDM_VIEW_AI_CHAT
    $dialogs = @((Get-NutshellWindows -Session $s) | Where-Object { $_ -match "`t#32770`t" })
    Assert-True ($dialogs.Count -eq 0) "a dialog appeared opening the panel with no API key: $($dialogs -join '; ')"
    $p = Get-NutshellAiPanel -Session $s
    Assert-True ($p -ne [IntPtr]::Zero) "AI Assist panel did not open with no API key"
    $path = Join-Path $Artifacts "ai_panel_no_key.png"
    Save-NutshellScreenshot -Session $s -Path $path | Out-Null
    Assert-True (Test-NutshellCaptureNonBlank -Path $path) "capture looks blank: ai_panel_no_key.png"
    "panel opened with no key (hwnd $p), no dialog"
}

# ---- Summary ------------------------------------------------------------------

$passed = @($results | Where-Object { $_.Passed }).Count
$failed = @($results | Where-Object { -not $_.Passed }).Count
Write-Host ""
Write-Host ("Integration: {0} passed, {1} failed" -f $passed, $failed)
$results | Format-Table -AutoSize | Out-String | Write-Host
if ($failed -gt 0) { exit 1 } else { exit 0 }
