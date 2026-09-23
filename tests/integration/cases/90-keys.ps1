# 90-keys.ps1 -- special keys (docs/superpowers/specs/2026-09-23-special-keys-design.md):
# F-keys, Alt+letter, Shift+Tab/Ctrl+arrows, Ctrl+Space, PgUp/PgDn by screen,
# the Send Key menu (including the "Send Next Key with Alt" one-shot),
# Backspace over SSH, and the Ctrl+W/T vs. Ctrl+Shift+W/T split.
# Dot-sourced by Run-Integration.ps1, which defines Invoke-Case/Assert-True/
# $Artifacts before sourcing tests\integration\cases\*.ps1 in name order; also
# depends on Get-NutshellTabStripRegion/Open-NutshellSecondTab/Select-NutshellTab/
# Get-NutshellRegionHash/Test-NutshellCaptureNonBlank (the last from the driver,
# same as cases\60-tabs-logging.ps1 and cases\50-window.ps1 use it).
#
# All nine cases below read `cat -v` (or, for the PgUp/PgDn alt-screen half,
# `less`) output from the session log rather than driving a real terminal
# emulator: `stty -echo -icanon` (icanon off so cat -v prints each byte as it
# arrives instead of waiting for a line; echo off so only cat -v's own
# rendering appears in the log, not the raw bytes echoed a second time by the
# tty) then `cat -v`, which renders a non-printable byte as its caret/meta
# notation (ESC -> "^[", NUL -> "^@", Ctrl+W -> "^W", ...) and everything else
# literally. ISIG stays on (stty -echo -icanon, not -isig), so Ctrl+C still
# sends SIGINT and ends cat -- that is how every case below closes its own
# harness instead of leaving cat running when the case (and the whole scratch
# process) is torn down. `stty sane; echo KEYS_END` is then typed as a new
# command: bash drops the rest of a command list once its foreground command
# dies of SIGINT, so it cannot follow `cat -v` on the starting line.
#
# Marker escaping: the command line that starts the harness is itself echoed
# by the *shell* while it is being typed (echo is still on at that point --
# stty -echo only takes effect once the shell executes it), so a naive
# "KEYS_BEGIN"/"KEYS_END" marker would appear twice: once in that echoed
# command line and once for real when `echo KEYS_BEGIN` actually runs. The
# harness therefore writes the echo argument as `KEYS_"BEGIN"` (and
# `KEYS_"END"`): the shell's quote removal turns that into the plain text
# KEYS_BEGIN/KEYS_END when `echo` actually runs, but the *echoed command line*
# still contains the quote characters, so an exact-line regex
# ("(?m)^KEYS_BEGIN\s*$", no quotes, nothing else on the line) only ever
# matches the real marker, never the typed command -- even if line-wrapping
# split the command such that "KEYS_BEGIN" (with its quotes) briefly starts a
# row of its own.

function Start-NutshellCatV {
    <# Start the cat -v harness (see file header) and wait for it to be ready
       to receive keys. #>
    param([Parameter(Mandatory)] $Session, [int] $TimeoutSec = 10)
    Send-NutshellLine -Session $Session -Line 'stty -echo -icanon; echo KEYS_"BEGIN"; cat -v' -SettleMs 400
    Assert-True (Wait-NutshellLog -Session $Session -Pattern '(?m)^KEYS_BEGIN\s*$' -TimeoutSec $TimeoutSec) `
        "cat -v harness never started (no KEYS_BEGIN marker in the session log)"
}

function Stop-NutshellCatV {
    <# End the cat -v harness with Ctrl+C (ISIG is still on), then type
       `stty sane; echo KEYS_END` as a fresh command line and wait for the
       marker, so the shell is left in cooked mode again before the next
       command in the case. It cannot ride on the starting command line:
       bash abandons the rest of a list whose foreground command died of
       SIGINT. With echo still off the typed line is not echoed, so only the
       real marker lands in the log. #>
    param([Parameter(Mandatory)] $Session, [int] $TimeoutSec = 10)
    Send-NutshellChord -Session $Session -Key C -Ctrl -SettleMs 500
    Send-NutshellLine -Session $Session -Line 'stty sane; echo KEYS_"END"' -SettleMs 300
    Assert-True (Wait-NutshellLog -Session $Session -Pattern '(?m)^KEYS_END\s*$' -TimeoutSec $TimeoutSec) `
        "cat -v harness never exited (no KEYS_END marker) -- Ctrl+C may not have reached it, or the shell is not sane"
}

function Get-NutshellKeysRegion {
    <# Text of the session log strictly between the last KEYS_BEGIN marker
       line and the next KEYS_END marker line after it (each matched as an
       exact, whitespace-trimmed line -- see the file header comment on why
       the quoted echo of the command itself can never match). $null if
       either marker isn't present yet. #>
    param([Parameter(Mandatory)] $Session)
    $t = Get-NutshellLogText -Session $Session
    $begins = [regex]::Matches($t, '(?m)^KEYS_BEGIN\s*$')
    if ($begins.Count -eq 0) { return $null }
    $last = $begins[$begins.Count - 1]
    $tail = $t.Substring($last.Index + $last.Length)
    $end = [regex]::Match($tail, '(?m)^KEYS_END\s*$')
    if (-not $end.Success) { return $null }
    return $tail.Substring(0, $end.Index)
}

# ---- F1-F12 encode as xterm's SS3/CSI sequences -----------------------------------
Invoke-Case "f_keys_reach_shell" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Start-NutshellCatV -Session $s
    # F10 is posted as WM_SYSKEYDOWN by Send-NutshellKey (matching how real
    # Windows delivers it); F11 alone is the fullscreen toggle, so it is sent
    # with Shift here to reach the shell as CSI 23;2~. A space after each key
    # separates the rendered sequences in the log without adding any bytes
    # cat -v would render specially.
    foreach ($k in @("F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10")) {
        Send-NutshellKey -Session $s -Key $k -SettleMs 250
        Send-NutshellText -Session $s -Text " " -SettleMs 100
    }
    Send-NutshellChord -Session $s -Key F11 -Shift -SettleMs 300
    Send-NutshellText -Session $s -Text " " -SettleMs 100
    Send-NutshellKey -Session $s -Key F12 -SettleMs 250
    Send-NutshellText -Session $s -Text " " -SettleMs 100
    Stop-NutshellCatV -Session $s

    $region = Get-NutshellKeysRegion -Session $s
    Assert-True ($null -ne $region) "no captured region between KEYS_BEGIN/KEYS_END"
    $tokens = @('^[OP', '^[OQ', '^[OR', '^[OS', '^[[15~', '^[[17~', '^[[18~', '^[[19~', `
                '^[[20~', '^[[21~', '^[[23;2~', '^[[24~')
    $pattern = ($tokens | ForEach-Object { [regex]::Escape($_) }) -join '\s+'
    Assert-True ($region -match $pattern) "F1-F10/Shift+F11/F12 did not arrive in the expected order ($($tokens -join ' ')): $region"
    "F1-F4 -> SS3 P-S, F5-F10 -> CSI 15/17/18/19/20/21~, Shift+F11 -> CSI 23;2~, F12 -> CSI 24~"
}

# ---- Alt+letter: exactly one ESC-prefixed character --------------------------------
Invoke-Case "alt_letter_reaches_shell" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Start-NutshellCatV -Session $s
    # No Ctrl: Send-NutshellChord posts WM_SYSKEYDOWN/WM_SYSKEYUP with the
    # context bit set, exactly as a real Alt+F does, and posts no WM_SYSCHAR
    # itself -- the app's own TranslateMessage must synthesise it, so a
    # double-send (two ^[f) would mean that synthesis, not the harness, is at
    # fault.
    Send-NutshellChord -Session $s -Key F -Alt -SettleMs 400
    Send-NutshellText -Session $s -Text "Z" -SettleMs 200
    Stop-NutshellCatV -Session $s

    $region = Get-NutshellKeysRegion -Session $s
    Assert-True ($null -ne $region) "no captured region between KEYS_BEGIN/KEYS_END"
    $hits = [regex]::Matches($region, [regex]::Escape('^[f'))
    Assert-True ($hits.Count -eq 1) "expected exactly one ^[f from Alt+F, got $($hits.Count) in: $region"
    Assert-True ($region -match ([regex]::Escape('^[f') + 'Z')) "Alt+F was not immediately followed by the plain 'Z' marker (stray bytes in between?): $region"
    "Alt+F -> exactly one ^[f, immediately followed by the marker"
}

# ---- Shift+Tab and Ctrl+arrows ------------------------------------------------------
Invoke-Case "shift_tab_and_ctrl_arrows" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Start-NutshellCatV -Session $s
    Send-NutshellChord -Session $s -Key Tab -Shift -SettleMs 250
    Send-NutshellText -Session $s -Text " " -SettleMs 100
    Send-NutshellChord -Session $s -Key Up -Ctrl -SettleMs 250
    Send-NutshellText -Session $s -Text " " -SettleMs 100
    Send-NutshellChord -Session $s -Key Down -Ctrl -SettleMs 250
    Send-NutshellText -Session $s -Text " " -SettleMs 100
    Send-NutshellChord -Session $s -Key Right -Ctrl -SettleMs 250
    Send-NutshellText -Session $s -Text " " -SettleMs 100
    Send-NutshellChord -Session $s -Key Left -Ctrl -SettleMs 250
    Send-NutshellText -Session $s -Text " " -SettleMs 100
    Stop-NutshellCatV -Session $s

    $region = Get-NutshellKeysRegion -Session $s
    Assert-True ($null -ne $region) "no captured region between KEYS_BEGIN/KEYS_END"
    $tokens = @('^[[Z', '^[[1;5A', '^[[1;5B', '^[[1;5C', '^[[1;5D')
    $pattern = ($tokens | ForEach-Object { [regex]::Escape($_) }) -join '\s+'
    Assert-True ($region -match $pattern) "Shift+Tab/Ctrl+arrows did not arrive in the expected order ($($tokens -join ' ')): $region"
    "Shift+Tab -> CSI Z; Ctrl+Up/Down/Right/Left -> CSI 1;5 A/B/C/D"
}

# ---- Ctrl+Space sends NUL and nothing else -----------------------------------------
Invoke-Case "ctrl_space_sends_nul_only" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Start-NutshellCatV -Session $s
    Send-NutshellChord -Session $s -Key Space -Ctrl -SettleMs 300
    Send-NutshellText -Session $s -Text "Z" -SettleMs 200   # no separator: the point is NUL immediately followed by Z
    Stop-NutshellCatV -Session $s

    $region = Get-NutshellKeysRegion -Session $s
    Assert-True ($null -ne $region) "no captured region between KEYS_BEGIN/KEYS_END"
    $hits = [regex]::Matches($region, [regex]::Escape('^@'))
    Assert-True ($hits.Count -eq 1) "expected exactly one NUL (^@) from Ctrl+Space, got $($hits.Count) in: $region"
    Assert-True ($region -match ([regex]::Escape('^@') + 'Z')) "Ctrl+Space did not send NUL immediately followed by the 'Z' marker (extra byte, e.g. a space, in between?): $region"
    "Ctrl+Space -> ^@ only, immediately followed by the marker"
}

# ---- PgUp/PgDn: local scrollback on the primary screen, paging on the alternate ----
Invoke-Case "pgup_pages_on_alt_screen_and_scrolls_on_primary" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    $tmpFile = $null
    $detailParts = New-Object System.Collections.ArrayList
    try {
        # ---- Primary screen: plain PgUp must not reach the shell at all ----
        Start-NutshellCatV -Session $s
        Send-NutshellKey -Session $s -Key PgUp -SettleMs 300
        Send-NutshellText -Session $s -Text "M" -SettleMs 200
        Stop-NutshellCatV -Session $s
        $region1 = Get-NutshellKeysRegion -Session $s
        Assert-True ($null -ne $region1) "no captured region for the primary-screen half"
        Assert-True ($region1 -match "M") "the 'M' marker never reached cat -v after plain PgUp on the primary screen"
        Assert-True ($region1 -notmatch [regex]::Escape('^[[5~')) "plain PgUp on the primary screen sent CSI 5~ to the shell (it should scroll local history instead, and send nothing): $region1"
        [void]$detailParts.Add("primary screen: PgUp did not reach the shell, only the 'M' marker did")

        # ---- Alternate screen (less): PgDn/PgUp must page the program -----
        Send-NutshellLine -Session $s -Line 'echo NSK_LINES=$(tput lines)' -SettleMs 300
        Assert-True (Wait-NutshellLog -Session $s -Pattern "NSK_LINES=(\d+)" -TimeoutSec 5) "no tput lines report"
        $termLines = [int][regex]::Match((Get-NutshellLogText -Session $s), "NSK_LINES=(\d+)").Groups[1].Value

        # $$ (the shell's own PID) must not be touched by PowerShell string
        # interpolation -- see cases\60-tabs-logging.ps1's 'kill -9 $$' for the
        # same care -- so it is captured via an echoed marker instead of
        # built into a PowerShell-interpolated string.
        Send-NutshellLine -Session $s -Line 'echo NSK_PID=$$' -SettleMs 300
        Assert-True (Wait-NutshellLog -Session $s -Pattern "NSK_PID=(\d+)" -TimeoutSec 5) "no PID captured for temp file naming"
        $shellPid = [regex]::Match((Get-NutshellLogText -Session $s), "NSK_PID=(\d+)").Groups[1].Value
        $tmpFile = "/tmp/nsk_alt_$shellPid.txt"

        Send-NutshellLine -Session $s -Line "seq 1 400 > $tmpFile" -SettleMs 400
        # LESS= clears any inherited LESS env var (e.g. a shell profile or
        # git's default "FRX") -- an inherited -X would keep less off the
        # alternate screen and defeat this whole half; see the special-keys
        # design spec section 3's note on `less -X`.
        Send-NutshellLine -Session $s -Line "LESS= less $tmpFile" -SettleMs 1200
        Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^1\s*$" -TimeoutSec 8) "less never drew its first screen (no line '1' seen in the log)"

        $lenBeforePgDn = (Get-NutshellLogText -Session $s).Length
        Send-NutshellKey -Session $s -Key PgDn -SettleMs 900
        $newAfterPgDn = (Get-NutshellLogText -Session $s).Substring($lenBeforePgDn)
        $numsDown = [regex]::Matches($newAfterPgDn, "(?m)^(\d+)\s*$") | ForEach-Object { [int]$_.Groups[1].Value }
        $maxDown = if ($numsDown.Count -gt 0) { ($numsDown | Measure-Object -Maximum).Maximum } else { 0 }
        # A line number this far past the first screen cannot be part of the
        # page `less` drew on entry for any plausible terminal height, so
        # seeing one proves PgDn paged the alternate-screen program rather
        # than being swallowed or scrolling (nonexistent) local history.
        $threshold = $termLines + 10
        Assert-True ($maxDown -gt $threshold) "PgDn in less (alternate screen) did not page: highest line number seen afterwards was $maxDown, expected > $threshold (terminal height $termLines)"
        [void]$detailParts.Add("alt screen: PgDn paged to line $maxDown (> $threshold, terminal height $termLines)")

        $lenBeforePgUp = (Get-NutshellLogText -Session $s).Length
        Send-NutshellKey -Session $s -Key PgUp -SettleMs 900
        $newAfterPgUp = (Get-NutshellLogText -Session $s).Substring($lenBeforePgUp)
        $numsUp = [regex]::Matches($newAfterPgUp, "(?m)^(\d+)\s*$") | ForEach-Object { [int]$_.Groups[1].Value }
        # Soft check only -- per the task brief, asserted when cleanly
        # detectable and documented rather than asserted when not: less may
        # repaint via a partial-screen scroll that never puts a low line
        # number back at column/row start on its own line, which would be a
        # harness-detection limitation, not a product bug.
        if ($numsUp.Count -gt 0 -and (($numsUp | Measure-Object -Minimum).Minimum -le 5)) {
            [void]$detailParts.Add("alt screen: PgUp returned toward the top (line $(($numsUp | Measure-Object -Minimum).Minimum) redrawn)")
        } else {
            [void]$detailParts.Add("alt screen: PgUp-returns-to-top not cleanly detectable from the log (less's redraw may not repaint a low line number on its own source line) -- not asserted; only PgDn's paging is a hard assertion here")
        }
    } finally {
        # Best-effort cleanup even on failure: quit less if it is still up
        # (harmless if it already isn't), interrupt cat -v if that is still
        # up, restore cooked mode, remove the temp file from the host.
        Send-NutshellText -Session $s -Text "q" -SettleMs 400
        Send-NutshellChord -Session $s -Key C -Ctrl -SettleMs 400
        Send-NutshellLine -Session $s -Line "stty sane" -SettleMs 300
        if ($tmpFile) { Send-NutshellLine -Session $s -Line "rm -f $tmpFile" -SettleMs 300 }
    }
    ($detailParts -join "; ")
}

# ---- Send Key menu: F1 item posts without any dialog up ---------------------------
Invoke-Case "send_key_menu_posts_f1" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Start-NutshellCatV -Session $s
    Send-NutshellCommand -Session $s -Id 2040 -SettleMs 400   # Edit > Send Key > F1
    Send-NutshellText -Session $s -Text " " -SettleMs 100
    Stop-NutshellCatV -Session $s

    $region = Get-NutshellKeysRegion -Session $s
    Assert-True ($null -ne $region) "no captured region between KEYS_BEGIN/KEYS_END"
    Assert-True ($region -match [regex]::Escape('^[OP')) "Edit > Send Key > F1 (WM_COMMAND 2040) did not write ESC O P to the active session: $region"
    "Send Key menu's F1 item (WM_COMMAND 2040) wrote ^[OP to the active session with no dialog up"
}

# ---- Ctrl+W/Ctrl+T reach the shell; Ctrl+Shift+W closes a tab ---------------------
Invoke-Case "ctrl_w_reaches_shell_and_shift_closes_tab" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null   # tab A's log -- stays $s.Log throughout
    Wait-NutshellShell -Session $s

    # ---- Plain Ctrl+W / Ctrl+T now reach the shell (moved off Ctrl+Shift+*) ----
    Start-NutshellCatV -Session $s
    Send-NutshellChord -Session $s -Key W -Ctrl -SettleMs 300
    Send-NutshellText -Session $s -Text " " -SettleMs 100
    Send-NutshellChord -Session $s -Key T -Ctrl -SettleMs 300
    Send-NutshellText -Session $s -Text " " -SettleMs 100
    Stop-NutshellCatV -Session $s
    $region1 = Get-NutshellKeysRegion -Session $s
    Assert-True ($null -ne $region1) "no captured region for the plain Ctrl+W/Ctrl+T half"
    $pattern = ([regex]::Escape('^W')) + '\s+' + ([regex]::Escape('^T'))
    Assert-True ($region1 -match $pattern) "plain Ctrl+W/Ctrl+T did not reach the shell as ^W/^T in order: $region1"

    # ---- Ctrl+Shift+W closes a tab, without leaking ^W into the survivor ----
    Set-NutshellWindowSize -Session $s -Width 1200 -Height 800 | Out-Null
    $stripRegion = Get-NutshellTabStripRegion -Session $s -Pad 0.005
    $before = Join-Path $Artifacts "ctrl_shift_w_1open.png"
    Save-NutshellScreenshot -Session $s -Path $before | Out-Null

    Open-NutshellSecondTab -Session $s   # posted end to end; tab B connects and becomes active
    Assert-True (((Get-NutshellWindows -Session $s) | Where-Object { $_ -match "Session Manager" }).Count -eq 0) "Session Manager still open; Connect failed"
    Start-Sleep -Milliseconds 800
    $afterOpen = Join-Path $Artifacts "ctrl_shift_w_2open.png"
    Save-NutshellScreenshot -Session $s -Path $afterOpen | Out-Null
    Assert-True ((Get-NutshellRegionHash -Path $before -Region $stripRegion) -ne (Get-NutshellRegionHash -Path $afterOpen -Region $stripRegion)) `
        "tab strip capture did not change after opening a second tab"

    # Start cat -v in tab A *while tab A is active* (so the harness command
    # actually reaches tab A's shell), then switch to tab B so the
    # Ctrl+Shift+W chord below -- an app-level shortcut decided independently
    # of any one tab's shell -- targets tab B, the one meant to close, while
    # tab A sits in the background still running cat -v: if the chord ever
    # regressed to leaking a plain ^W the way old Ctrl+W did, it would show up
    # directly in tab A's own log.
    Select-NutshellTab -Session $s -Index 0
    Start-NutshellCatV -Session $s
    Select-NutshellTab -Session $s -Index 1

    Send-NutshellChord -Session $s -Key W -Ctrl -Shift -SettleMs 2500   # closes tab B; tab A becomes active again

    $afterClose = Join-Path $Artifacts "ctrl_shift_w_3closed.png"
    Save-NutshellScreenshot -Session $s -Path $afterClose | Out-Null
    Assert-True (Test-NutshellCaptureNonBlank -Path $afterClose) "capture looks blank after closing tab B: $afterClose"
    Assert-True ((Get-NutshellRegionHash -Path $afterClose -Region $stripRegion) -eq (Get-NutshellRegionHash -Path $before -Region $stripRegion)) `
        "tab strip after Ctrl+Shift+W does not match the one-tab strip from before tab B was opened"

    # Tab A is active again once tab B closes, so this marker and the
    # cat -v teardown below both now reach tab A.
    Send-NutshellText -Session $s -Text " " -SettleMs 200
    Stop-NutshellCatV -Session $s
    $region2 = Get-NutshellKeysRegion -Session $s
    Assert-True ($null -ne $region2) "no captured region for tab A's cat -v around the Ctrl+Shift+W close"
    Assert-True ($region2 -notmatch [regex]::Escape('^W')) "a ^W leaked into tab A's shell from the Ctrl+Shift+W chord that closed tab B: $region2"

    Send-NutshellLine -Session $s -Line "echo TAB_A_STILL_ACTIVE_AFTER_SHIFT_W_CLOSE"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "TAB_A_STILL_ACTIVE_AFTER_SHIFT_W_CLOSE" -TimeoutSec 10) `
        "marker typed after Ctrl+Shift+W closed tab B never reached tab A's log -- tab A is not the active tab"
    "plain Ctrl+W/Ctrl+T -> ^W/^T; Ctrl+Shift+W closed tab B (strip back to one tab), no ^W leaked into tab A, tab A active again"
}

# ---- Send Key menu: "Send Next Key with Alt" prefixes the next key with ESC ------
Invoke-Case "send_key_alt_next_prefixes_escape" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Start-NutshellCatV -Session $s
    # IDM_SENDKEY_ALT_NEXT (src/ui/resource.h) arms the one-shot; the 'f' that
    # follows is posted as a plain WM_KEYDOWN/WM_KEYUP with no Alt held at
    # all -- the one-shot, not a real Alt chord, is what must supply the ESC
    # prefix, unlike alt_letter_reaches_shell above.
    Send-NutshellCommand -Session $s -Id 2059 -SettleMs 400   # Edit > Send Key > Send Next Key with Alt
    Send-NutshellKey -Session $s -Key F -SettleMs 250
    Send-NutshellText -Session $s -Text " " -SettleMs 100
    Stop-NutshellCatV -Session $s

    $region = Get-NutshellKeysRegion -Session $s
    Assert-True ($null -ne $region) "no captured region between KEYS_BEGIN/KEYS_END"
    $hits = [regex]::Matches($region, [regex]::Escape('^[f'))
    Assert-True ($hits.Count -eq 1) "expected exactly one ^[f from the Send Next Key with Alt one-shot, got $($hits.Count) in: $region"
    "Send Key menu's Send Next Key with Alt (WM_COMMAND 2059) armed the one-shot; a plain 'f' (no Alt held) arrived as ^[f"
}

# ---- Backspace sends BS (0x08) over SSH --------------------------------------------
Invoke-Case "backspace_sends_bs_over_ssh" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Start-NutshellCatV -Session $s
    Send-NutshellKey -Session $s -Key Backspace -SettleMs 250
    Send-NutshellText -Session $s -Text " " -SettleMs 100
    Stop-NutshellCatV -Session $s

    $region = Get-NutshellKeysRegion -Session $s
    Assert-True ($null -ne $region) "no captured region between KEYS_BEGIN/KEYS_END"
    $hits = [regex]::Matches($region, [regex]::Escape('^H'))
    Assert-True ($hits.Count -eq 1) "expected exactly one ^H (BS, 0x08) from Backspace over SSH, got $($hits.Count) in: $region"
    "Backspace -> ^H (0x08) over SSH"
}
