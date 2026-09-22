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
