# 20-ai.ps1 -- AI Assist cases. The five key-gated ai_* cases (tier "ai", real
# API calls, skipped without a key -- see Invoke-AiCase in Run-Integration.ps1)
# plus ai_panel_opens_without_key (tier "bvt", no key needed despite the name).
# Dot-sourced by Run-Integration.ps1; depends on its Invoke-Case/Invoke-AiCase/
# Assert-True/$Artifacts/$WM_COMMAND.

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
