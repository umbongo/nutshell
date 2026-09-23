# 80-local-shell.ps1 -- the local shell (docs/superpowers/specs/
# 2026-09-22-local-shell-design.md section 8, the `local_shell` gate case).
# Dot-sourced by Run-Integration.ps1; depends on its Invoke-Case/Assert-True/
# $Artifacts/$results and on NutshellIT.psm1's posted-input helpers.
#
# This is the one case in the suite that needs no SSH host at all: it
# launches with --local, which starts a session from a transient local
# profile without looking any saved name up, so whatever New-NutshellTestEnv
# wrote into nutshell.config is irrelevant to it. It still needs -HostName /
# -User / -KeyPath on the command line, because the driver validates them
# before any case runs.

function Get-NutshellNewestLog {
    <# The most recently written .log in the session's log directory, or $null.
       Reconnecting closes the old session log and opens a new one, so a case
       that reconnects cannot keep using $Session.Log. #>
    param([Parameter(Mandatory)] $Session)
    $f = Get-ChildItem -Path $Session.Env.Logs -Filter *.log -ErrorAction SilentlyContinue |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($f) { return $f.FullName }
    return $null
}

# ---- local_shell: --local runs a shell, exits, and reconnects ------------------
Invoke-Case "local_shell" @{} {
    param($s)

    # 1. A shell is running and its bytes reach the terminal.
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    $marker = "nutshell-" + (Get-Random -Minimum 100000 -Maximum 999999)
    Send-NutshellLine -Session $s -Line ("echo " + $marker)
    Assert-True (Wait-NutshellLog -Session $s -Pattern ([regex]::Escape($marker)) -TimeoutSec 15) `
        "the local shell never echoed '$marker' into the session log -- no shell, or its output is not reaching the terminal"

    # `echo nutshell-$$` as the spec writes it: proof the shell is really
    # expanding, not a remote host echoing a literal.
    Send-NutshellLine -Session $s -Line 'echo pid-$$'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "pid-\d+" -TimeoutSec 15) `
        "the local shell did not expand \$\$ -- 'echo pid-`$`$' produced no pid-<number>"

    # 2. `exit` ends the shell. window.c writes "[Connection Closed]" into the
    #    *terminal* on EOF, not through the transport, so it never reaches the
    #    session log -- the log is the oracle here, so the evidence is that
    #    nothing typed afterwards reaches it any more: with io.ctx cleared,
    #    WM_CHAR has nowhere to write.
    Send-NutshellLine -Session $s -Line "exit"
    Start-Sleep -Seconds 2
    $gone = "gone-" + (Get-Random -Minimum 100000 -Maximum 999999)
    Send-NutshellLine -Session $s -Line ("echo " + $gone)
    Assert-True (-not (Wait-NutshellLog -Session $s -Pattern ([regex]::Escape($gone)) -TimeoutSec 5)) `
        "the shell was still echoing after 'exit' -- the session did not end"
    $oldLog = $s.Log

    # 3. Reconnect. The status dot's click and the File > Disconnect menu item
    #    are the same handler (on_status_click, reached from IDM_FILE_DISCONNECT
    #    with the tab's current status), so a posted WM_COMMAND is the whole
    #    gesture -- no foreground window needed.
    #
    #    The absence of a dialog is itself the proof that the tab really is
    #    DISCONNECTED: that same command on a CONNECTED tab puts up
    #    "Disconnect this session?" and waits. On a DISCONNECTED one it
    #    re-spawns, and a local profile has no host, no password and no
    #    host-key prompt, so nothing should appear.
    Send-NutshellCommand -Session $s -Id 2003 -SettleMs 1500
    $dlg = Wait-NutshellDialog -Session $s -TimeoutSec 2
    Assert-True ($dlg -eq [IntPtr]::Zero) `
        "a dialog appeared instead of a silent reconnect -- either the tab was still CONNECTED (so 'exit' did not disconnect it) or the local path is prompting for something"

    # Reconnecting closed the old session log and did not open a new one --
    # the same as the SSH path, which only auto-opens one when the
    # logging_enabled setting is on, and this test config leaves it off (the
    # harness turns logging on per session through the File menu instead).
    # So turn it on again; the new file is proof in itself that the tab is
    # live again, since on_log_toggle names it after the profile.
    $newLog = Start-NutshellLogging -Session $s
    Assert-True ($newLog -ne $oldLog) `
        "logging after reconnect reused the old file '$oldLog' -- the first session's log was never closed"

    Wait-NutshellShell -Session $s -TimeoutSec 30
    $marker2 = "again-" + (Get-Random -Minimum 100000 -Maximum 999999)
    Send-NutshellLine -Session $s -Line ("echo " + $marker2)
    Assert-True (Wait-NutshellLog -Session $s -Pattern ([regex]::Escape($marker2)) -TimeoutSec 15) `
        "the re-spawned local shell did not echo '$marker2' -- reconnect produced no working shell"

    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "local_shell.png") | Out-Null
    "--local ran a shell (echo + PID expansion), exit ended it (nothing echoed after), reconnect re-spawned it silently into a fresh log and echoed again"
} -ExtraArgs @("--local")

# ---- local_shell_powershell: a saved local profile whose shell is PowerShell ---
# Unlike local_shell (--local, transient profile, no shell override -> busybox/
# Git bash/MSYS2), this connects by name (-sn ps) to a saved profile of kind
# "local" whose shell field is "powershell.exe -NoLogo" (src/config/loader.c's
# profile loader reads both fields; --local never sets a shell override, so
# PowerShell can only be reached through a saved profile). The profile is
# injected via New-NutshellTestEnv's -ExtraProfiles (passed through
# Invoke-Case), which appends it after the generated SSH profile without
# touching that profile or any other case's config.

function Wait-NutshellPowerShellPrompt {
    <# Like Wait-NutshellShell, but for a "PS <path>> " prompt instead of a
       bash-style "$"/"#" one -- Wait-NutshellShell's '[$#]\s*$' pattern never
       matches PowerShell's prompt, which ends in ">". Presses Enter (posted)
       until the pattern shows up in the log. Throws on timeout. #>
    param([Parameter(Mandatory)] $Session, [int] $TimeoutSec = 30)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Send-NutshellKey -Session $Session -Key Enter -SettleMs 700
        if ((Get-NutshellLogText -Session $Session) -match 'PS [A-Za-z]:\\[^\r\n]*>\s*$') { return }
    }
    throw "no PowerShell prompt within ${TimeoutSec}s (still starting, or the shell field did not launch powershell.exe)"
}

Invoke-Case "local_shell_powershell" @{ paste_confirm = $false } {
    param($s)

    # 1. PowerShell starts and its prompt appears.
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellPowerShellPrompt -Session $s
    Assert-True ((Get-NutshellLogText -Session $s) -match 'PS [A-Za-z]:\\[^\r\n]*>') `
        "no 'PS <path>>' prompt in the session log after startup"

    # 2. A two-line paste runs both lines. Each marker is computed inside the
    #    pasted command (Write-Output ("PSPn_" + <number>)) so its output text
    #    never appears as a literal substring of the echoed command line --
    #    only the command's actual stdout can satisfy the pattern.
    $n1 = Get-Random -Minimum 1000 -Maximum 8999
    $n2 = $n1 + 1
    $out1 = "PSP1_$n1"
    $out2 = "PSP2_$n2"
    $line1 = 'Write-Output ("PSP1_" + {0})' -f $n1
    $line2 = 'Write-Output ("PSP2_" + {0})' -f $n2
    # window.c's paste_chunk_write turns each "\n" of a local paste into "\r",
    # the key that submits a line in PowerShell. The trailing "`r`n" is part of
    # the clipboard text on purpose: without it the second line is typed but
    # never submitted, the same as in any terminal.
    Set-Clipboard -Value ($line1 + "`r`n" + $line2 + "`r`n")
    # Ctrl+V is decided in WM_KEYDOWN via GetKeyState, same as
    # paste_without_confirmation -- see that case's comment.
    Send-NutshellChord -Session $s -Key V -Ctrl -SettleMs 800
    # No Enter is pressed here. Had PSReadLine turned on bracketed paste
    # (ESC[?2004h), window.c would wrap the paste in ESC[200~ ... ESC[201~ and
    # PSReadLine would hold both lines in one edit buffer waiting for a real
    # Enter -- so both outputs appearing unaided is also the evidence that it
    # does not, under ConPTY.
    Assert-True (Wait-NutshellLog -Session $s -Pattern ([regex]::Escape($out1)) -TimeoutSec 8) `
        "the pasted first line never produced its output ($out1)"
    Assert-True (Wait-NutshellLog -Session $s -Pattern ([regex]::Escape($out2)) -TimeoutSec 8) `
        "the pasted second line never produced its output ($out2) -- its '\r' did not submit it"

    # 3. Ctrl+C interrupts a running command.
    Send-NutshellLine -Session $s -Line "Start-Sleep 30"
    Start-Sleep -Milliseconds 1000
    Send-NutshellChord -Session $s -Key C -Ctrl -SettleMs 500
    $n3 = Get-Random -Minimum 1000 -Maximum 8999
    $n4 = $n3 + 1
    $out3 = "PSC1_$n3"
    $out4 = "PSC2_$n4"
    Send-NutshellLine -Session $s -Line ('Write-Output ("PSC1_" + {0})' -f $n3)
    Assert-True (Wait-NutshellLog -Session $s -Pattern ([regex]::Escape($out3)) -TimeoutSec 5) `
        "shell did not return within 5s: Ctrl+C did not interrupt Start-Sleep 30"
    Send-NutshellLine -Session $s -Line ('Write-Output ("PSC2_" + {0})' -f $n4)
    Assert-True (Wait-NutshellLog -Session $s -Pattern ([regex]::Escape($out4)) -TimeoutSec 5) `
        "the shell did not run a second command after the interrupt"

    # 4. `exit` ends the session -- same technique as local_shell step 2: the
    #    log is the oracle, so the evidence is that nothing typed afterwards
    #    reaches it.
    Send-NutshellLine -Session $s -Line "exit"
    Start-Sleep -Seconds 2
    $gone = "PSGONE_" + (Get-Random -Minimum 100000 -Maximum 999999)
    Send-NutshellLine -Session $s -Line ("echo " + $gone)
    Assert-True (-not (Wait-NutshellLog -Session $s -Pattern ([regex]::Escape($gone)) -TimeoutSec 5)) `
        "the shell was still echoing after 'exit' -- the session did not end"

    # A posted IDM_FILE_DISCONNECT with no dialog is proof the tab is
    # DISCONNECTED (same command on a CONNECTED tab prompts first) -- mirrors
    # local_shell step 3; no reconnect needed here.
    Send-NutshellCommand -Session $s -Id 2003 -SettleMs 1500
    $dlg = Wait-NutshellDialog -Session $s -TimeoutSec 2
    Assert-True ($dlg -eq [IntPtr]::Zero) `
        "a dialog appeared on IDM_FILE_DISCONNECT -- the tab was still CONNECTED, so 'exit' did not end the PowerShell session"

    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "local_shell_powershell.png") | Out-Null
    "PowerShell prompt appeared; two-line paste ran both lines with no extra Enter; Ctrl+C interrupted Start-Sleep 30; exit ended the session with no dialog on disconnect"
} -ExtraArgs @("-sn", "ps") -ExtraProfiles @(@{ name = "ps"; kind = "local"; shell = "powershell.exe -NoLogo" })
