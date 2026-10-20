# 10-terminal.ps1 -- basic terminal/session cases (connect, Ctrl+C, logging
# filename, paste, PTY resize, scrollback, smart scroll, inactive-tab resize).
# Dot-sourced by Run-Integration.ps1, which defines Invoke-Case/Assert-True/
# $Artifacts/$results before sourcing tests\integration\cases\*.ps1 in name order.

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
    # Ctrl+C with nothing selected: window.c copies nothing and deliberately
    # falls through, so the 0x03 that TranslateMessage derives from the key --
    # from the same attached keyboard state Send-NutshellChord sets -- reaches
    # the shell as SIGINT. Exactly one 0x03 is sent, so `sleep` is interrupted
    # once and the next line runs at a live prompt rather than a second
    # interrupt landing on it.
    Send-NutshellChord -Session $s -Key C -Ctrl -SettleMs 500
    Send-NutshellLine -Session $s -Line "echo AFTER_$((1+1))"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "AFTER_2" -TimeoutSec 5) "shell did not return within 5s: Ctrl+C was not delivered as SIGINT"
    # The prompt must be healthy afterwards: a chord delivered twice, or a
    # stray 0x03 arriving late, would kill this second command instead.
    Send-NutshellLine -Session $s -Line "echo STILL_ALIVE_3"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "STILL_ALIVE_3" -TimeoutSec 5) "the shell did not run a second command after the interrupt"
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
    # Ctrl+V is decided entirely in WM_KEYDOWN (do_paste, then return 0), so no
    # WM_CHAR is involved at all -- unlike Ctrl+C. If the chord's modifier did
    # not take, the key would fall through as a plain 'v' and the text below
    # would never appear.
    Send-NutshellChord -Session $s -Key V -Ctrl -SettleMs 600
    $wins = Get-NutshellWindows -Session $s
    Assert-True (-not ($wins | Where-Object { $_ -match "Paste" })) "a paste confirmation window appeared although paste_confirm is off"
    Send-NutshellKey -Session $s -Key Enter
    Assert-True (Wait-NutshellLog -Session $s -Pattern "PASTE_OK_42" -TimeoutSec 5) "pasted text never reached the shell"
    "pasted straight through"
}

Invoke-Case "paste_with_confirmation_shows_dialog" @{ paste_confirm = $true } {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Set-Clipboard -Value "echo PASTE_CONFIRM_7"
    Send-NutshellChord -Session $s -Key V -Ctrl -SettleMs 800
    $dlg = Get-NutshellWindows -Session $s | Where-Object { $_ -notmatch "Nutshell_Window" } | Select-Object -First 1
    Assert-True ($null -ne $dlg) "no confirmation window appeared with paste_confirm on"
    $dlgHwnd = [IntPtr][long]($dlg -split "`t")[0]
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "paste_confirm_dialog.png") -Hwnd ([long]($dlg -split "`t")[0]) | Out-Null
    # Dismiss with a posted Escape aimed at the dialog itself: paste_dlg.c runs
    # its own modal loop and cancels on a WM_KEYDOWN of VK_ESCAPE, so this needs
    # no foreground either. (The app's UI thread is inside that loop, hence a
    # posted key rather than a menu command.)
    Send-NutshellKey -Session $s -Key Escape -Hwnd $dlgHwnd -SettleMs 600
    Assert-True (-not (Get-NutshellWindows -Session $s | Where-Object { $_ -notmatch "Nutshell_Window" })) "the paste confirmation window is still open after Escape"
    Assert-True ((Get-NutshellLogText -Session $s) -notmatch "PASTE_CONFIRM_7") "the cancelled paste reached the shell anyway"
    "dialog shown: " + ($dlg -split "`t")[2]
}

# Confirm Paste takes Enter (spec 2026-10-20 section 3.5): Paste has the focus
# when the dialog opens. The pasted command computes its marker, so the
# marker reaches the log only if the command ran, not from the echo of the
# paste itself.
Invoke-Case "paste_with_confirmation_enter_pastes" @{ paste_confirm = $true } {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Set-Clipboard -Value 'echo PASTE_ENTER_$((4*2))'
    Send-NutshellChord -Session $s -Key V -Ctrl -SettleMs 300
    $dlg = Wait-NutshellDialog -Session $s -Title "Confirm Paste" -TimeoutSec 5
    Assert-True ($dlg -ne [IntPtr]::Zero) "no Confirm Paste dialog appeared"
    Send-NutshellKey -Session $s -Key Enter -Hwnd $dlg -SettleMs 600
    Assert-True (-not (Get-NutshellWindows -Session $s | Where-Object { $_ -match "Nutshell_PastePreview" })) "the dialog is still open after Enter"
    Send-NutshellKey -Session $s -Key Enter
    Assert-True (Wait-NutshellLog -Session $s -Pattern "PASTE_ENTER_8" -TimeoutSec 5) "Enter in the dialog did not paste: the command never ran"
    "Enter pasted"
}

# Review Focus 2: Tab moves the focus to Cancel, and Enter then presses Cancel.
Invoke-Case "paste_with_confirmation_tab_enter_cancels" @{ paste_confirm = $true } {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Set-Clipboard -Value 'echo PASTE_TAB_$((5*2))'
    Send-NutshellChord -Session $s -Key V -Ctrl -SettleMs 300
    $dlg = Wait-NutshellDialog -Session $s -Title "Confirm Paste" -TimeoutSec 5
    Assert-True ($dlg -ne [IntPtr]::Zero) "no Confirm Paste dialog appeared"
    Send-NutshellKey -Session $s -Key Tab -Hwnd $dlg -SettleMs 300
    Send-NutshellKey -Session $s -Key Enter -Hwnd $dlg -SettleMs 600
    Assert-True (-not (Get-NutshellWindows -Session $s | Where-Object { $_ -match "Nutshell_PastePreview" })) "the dialog is still open after Tab, Enter"
    Send-NutshellKey -Session $s -Key Enter -SettleMs 1000
    Assert-True ((Get-NutshellLogText -Session $s) -notmatch "PASTE_TAB_10") "Enter on the focused Cancel button pasted anyway"
    "Tab, Enter cancelled"
}

Invoke-Case "pty_resizes_with_window" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    # Both sizes come from the work area, not from two hardcoded pixel sizes:
    # SIZE_A is as large as this desktop allows and SIZE_B is 60% of it, so the
    # window demonstrably shrinks on a 1280x720 logon session and on a 4K one
    # alike. Set-NutshellWindowSize clamps to the work area and reports what it
    # achieved -- assert on that before asking the shell anything, so "the PTY
    # did not shrink" can never really mean "the window did not shrink".
    $wa = Get-NutshellWorkArea
    $ra = Set-NutshellWindowSize -Session $s -Width $wa.Width -Height $wa.Height
    Send-NutshellLine -Session $s -Line 'echo SIZE_A=$(tput lines)x$(tput cols)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "SIZE_A=(\d+)x(\d+)" -TimeoutSec 5) "no size report at first size"
    $rb = Set-NutshellWindowSize -Session $s -Width ([int]($wa.Width * 0.6)) -Height ([int]($wa.Height * 0.6))
    Assert-True ($rb.ClientWidth -lt $ra.ClientWidth -and $rb.ClientHeight -lt $ra.ClientHeight) `
        "the window did not actually shrink: client $($ra.ClientWidth)x$($ra.ClientHeight) -> $($rb.ClientWidth)x$($rb.ClientHeight) (work area $($wa.Width)x$($wa.Height))"
    Send-NutshellLine -Session $s -Line 'echo SIZE_B=$(tput lines)x$(tput cols)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "SIZE_B=(\d+)x(\d+)" -TimeoutSec 5) "no size report at second size"
    $t = Get-NutshellLogText -Session $s
    $a = [regex]::Match($t, "SIZE_A=(\d+)x(\d+)"); $b = [regex]::Match($t, "SIZE_B=(\d+)x(\d+)")
    Assert-True ([int]$b.Groups[1].Value -lt [int]$a.Groups[1].Value) "rows did not shrink: $($a.Value) -> $($b.Value)"
    Assert-True ([int]$b.Groups[2].Value -lt [int]$a.Groups[2].Value) "cols did not shrink: $($a.Value) -> $($b.Value)"
    "client $($ra.ClientWidth)x$($ra.ClientHeight) -> $($rb.ClientWidth)x$($rb.ClientHeight): $($a.Value) -> $($b.Value)"
}

Invoke-Case "page_up_scrolls_history" @{} {
    param($s)
    # No programmatic read of the screen exists yet; this case produces the
    # evidence screenshots (before/after Page Up) for eyeballing. Plain PgUp
    # on the primary screen no longer reaches the shell at all (it only
    # scrolls local history; see 90-keys.ps1's
    # pgup_pages_on_alt_screen_and_scrolls_on_primary for the assertion on
    # that), so scrolling is driven with Shift+PgUp, which always scrolls
    # history regardless of screen.
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line "seq 1 300"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^300\s*$" -TimeoutSec 5) "seq output incomplete"
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "page_up_before.png") | Out-Null
    Send-NutshellChord -Session $s -Key PgUp -Shift -SettleMs 400
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
    # Plain PgUp on the primary screen no longer reaches the shell (see
    # cases\90-keys.ps1's pgup_pages_on_alt_screen_and_scrolls_on_primary), so
    # Shift+PgUp -- which always scrolls history -- drives the scroll-back.
    Send-NutshellLine -Session $s -Line "(sleep 4; seq 1000 1100) &"
    Send-NutshellChord -Session $s -Key PgUp -Shift -SettleMs 250
    Send-NutshellChord -Session $s -Key PgUp -Shift -SettleMs 500
    $before = Join-Path $Artifacts "smart_scroll_before.png"
    $after  = Join-Path $Artifacts "smart_scroll_after.png"
    $live   = Join-Path $Artifacts "smart_scroll_live.png"
    Save-NutshellScreenshot -Session $s -Path $before | Out-Null
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^1100\s*$" -TimeoutSec 12) "the background output never arrived"
    Start-Sleep -Milliseconds 600
    Save-NutshellScreenshot -Session $s -Path $after | Out-Null
    Assert-True ((Get-TerminalAreaHash $before) -eq (Get-TerminalAreaHash $after)) "the scrolled-back view moved when new output arrived (see smart_scroll_before/after.png)"
    Send-NutshellKey -Session $s -Key Enter -SettleMs 600
    Save-NutshellScreenshot -Session $s -Path $live | Out-Null
    Assert-True ((Get-TerminalAreaHash $after) -ne (Get-TerminalAreaHash $live)) "a keypress did not return to the live view"
    "view held while 101 lines arrived; Enter returned to the live view"
}

Invoke-Case "resize_applies_to_inactive_tab" @{} {
    param($s)
    # Regression for "lost lines after resize": WM_SIZE only resized the active
    # tab, so a background tab kept its old grid and PTY size until the next
    # resize. The invariant that proves it is "the tab that was in the
    # background ends up with the same grid as the one in front, and a bigger
    # grid than it had before the resize" -- not "a 1300px-tall window gives at
    # least 30 rows", which only held on a desktop tall enough to grant that
    # request. Set-NutshellWindowSize now clamps to the work area, so the case
    # shrinks first and then grows to the largest size this desktop allows, and
    # compares achieved numbers.
    #
    # Tab B cannot report into the log this case watches ($s.Log is tab A's,
    # captured before tab B existed), so tab B writes its grid to a file on the
    # host and tab A cats it back.
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    $wa = Get-NutshellWorkArea
    $small = Set-NutshellWindowSize -Session $s -Width ([int]($wa.Width * 0.55)) -Height ([int]($wa.Height * 0.55))
    Send-NutshellLine -Session $s -Line 'echo TAB_A_READY=$(tput lines)x$(tput cols)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "TAB_A_READY=(\d+)x(\d+)" -TimeoutSec 5) "tab A not ready"
    $pre = [regex]::Match((Get-NutshellLogText -Session $s), "TAB_A_READY=(\d+)x(\d+)")
    $preLines = [int]$pre.Groups[1].Value; $preCols = [int]$pre.Groups[2].Value

    Open-NutshellSecondTab -Session $s
    Assert-True (((Get-NutshellWindows -Session $s) | Where-Object { $_ -match "Session Manager" }).Count -eq 0) "Session Manager still open; Connect failed"

    # The resize happens while tab B is the active one; tab A is the background
    # tab under test.
    $big = Set-NutshellWindowSize -Session $s -Width $wa.Width -Height $wa.Height
    Assert-True (($big.ClientWidth -gt $small.ClientWidth) -and ($big.ClientHeight -gt $small.ClientHeight)) `
        "the window did not actually grow: client $($small.ClientWidth)x$($small.ClientHeight) -> $($big.ClientWidth)x$($big.ClientHeight) (work area $($wa.Width)x$($wa.Height))"

    $bFile = "~/.nutshell_it_tab_b_grid"
    $bMatch = $null
    for ($try = 1; $try -le 4 -and $null -eq $bMatch; $try++) {
        Select-NutshellTab -Session $s -Index 1
        Send-NutshellLine -Session $s -Line "echo B_GRID=`$(tput lines)x`$(tput cols) > $bFile" -SettleMs 1500
        Select-NutshellTab -Session $s -Index 0
        # Sentinel first: only tab A's session log is being watched, so if the
        # switch back did not take effect this line goes to tab B and nothing
        # arrives -- which is a different fault from "the resize never reached
        # the inactive tab". Numbered so a later attempt cannot be satisfied by
        # an earlier attempt's sentinel.
        Send-NutshellLine -Session $s -Line "echo BACK_ON_TAB_A_$try"
        Assert-True (Wait-NutshellLog -Session $s -Pattern "BACK_ON_TAB_A_$try" -TimeoutSec 5) "the switch back to tab A did not take effect: typing still reached the other tab"
        # ...and the mirror check: the tab-B line must not have been typed into
        # tab A, or the file would hold tab A's own grid and the comparison
        # below would pass for the wrong reason.
        Assert-True ((Get-NutshellLogText -Session $s) -notmatch 'B_GRID=\$\(tput') "the switch to tab B did not take effect: the tab-B command was typed into tab A"
        Send-NutshellLine -Session $s -Line "cat $bFile"
        if (Wait-NutshellLog -Session $s -Pattern "B_GRID=(\d+)x(\d+)" -TimeoutSec 6) {
            $all = [regex]::Matches((Get-NutshellLogText -Session $s), "B_GRID=(\d+)x(\d+)")
            $bMatch = $all[$all.Count - 1]
        }
        # Tab B's login shell can still be in its MOTD scripts when the resize
        # lands (Wait-NutshellShell only ever watches tab A), so retry rather
        # than fail on the first miss.
    }
    Assert-True ($null -ne $bMatch) "tab B never reported its grid into $bFile after four attempts"
    $bLines = [int]$bMatch.Groups[1].Value; $bCols = [int]$bMatch.Groups[2].Value

    Send-NutshellLine -Session $s -Line 'echo A_GRID=$(tput lines)x$(tput cols)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "A_GRID=(\d+)x(\d+)" -TimeoutSec 5) "no size report from tab A"
    $aM = [regex]::Match((Get-NutshellLogText -Session $s), "A_GRID=(\d+)x(\d+)")
    $aLines = [int]$aM.Groups[1].Value; $aCols = [int]$aM.Groups[2].Value
    Send-NutshellLine -Session $s -Line "rm -f $bFile" -SettleMs 300
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "inactive_tab_after_resize.png") | Out-Null

    Assert-True (($aLines -gt $preLines) -and ($aCols -gt $preCols)) `
        "tab A still has the pre-resize grid: ${aLines}x${aCols}, was ${preLines}x${preCols} before the window grew"
    Assert-True (($aLines -eq $bLines) -and ($aCols -eq $bCols)) `
        "the background tab did not get the active tab's grid: tab A ${aLines}x${aCols}, tab B ${bLines}x${bCols}"
    "tab A ${preLines}x${preCols} -> ${aLines}x${aCols} after a resize made on tab B, matching tab B's ${bLines}x${bCols}"
}

# ---- Terminal selection, PuTTY style (spec docs/superpowers/specs/2026-10-20-putty-selection-design.md 3.2) ----
# Gate tier. Debug logging is on: render at trace for the render.draw
# geometry, general at debug for the sel.* events. Every point is a cell of
# that geometry, never a pixel literal (the dev box runs at 288 DPI). The
# clipboard is read raw and compared with CRLF intact. Start-SelCase clears
# the screen, so a typed command sits on row 0 and its output starts on row 1.

$script:SelSettings = @{ debug_enabled = $true; debug_level_render = "trace"; debug_level_general = "debug"; debug_log_dir = "debuglogs" }

function Get-SelClipboard {
    $t = Get-Clipboard -Raw
    if ($null -eq $t) { return "" }
    return [string]$t
}

function Start-SelCase {
    <# Session log on, shell ready, screen cleared; returns the grid geometry. #>
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line "clear" -SettleMs 800
    $g = Get-NutshellTermGeometry -Session $s
    Assert-True ($g.Cols -ge 60) "the terminal is too narrow for the selection cases: $($g.Cols) columns"
    return $g
}

function Assert-SelConsecutive {
    <# Every line is an integer one more than the line before; returns the first. #>
    param([string[]] $Lines)
    $first = 0
    Assert-True ([int]::TryParse($Lines[0], [ref]$first)) "the copy does not start with a number: '$($Lines[0])'"
    for ($i = 1; $i -lt $Lines.Count; $i++) {
        Assert-True ($Lines[$i] -eq [string]($first + $i)) "line $i is '$($Lines[$i])', expected $($first + $i)"
    }
    return $first
}

Invoke-Case "select_drag_past_bottom_copies_history" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line "seq 1 300"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^300\s*$" -TimeoutSec 5) "seq output incomplete"
    Start-Sleep -Milliseconds 500
    Send-NutshellWheel -Session $s -Lines 100
    $from  = Get-NutshellCellPoint -Geo $g -Row 0 -Col 0
    $below = Get-NutshellCellPoint -Geo $g -Row ($g.Rows + 1.5) -Col 0   # two cell heights past the bottom edge
    Send-NutshellDrag -Session $s -From $from -To $below -Hold
    $atBottom = Wait-NutshellDebugLog -Session $s -Pattern 'sel\.scroll .*offset=0\b' -TimeoutSec 10
    Send-NutshellDragEnd -Session $s -At $below
    Assert-True $atBottom "the drag never auto-scrolled back to the bottom (no sel.scroll offset=0)"
    $clip = Get-SelClipboard
    Assert-True ($clip -notmatch "(?<!`r)`n") "a line ending in the copy is not CRLF"
    $lines = @($clip -split "`r`n")
    Assert-True ($lines.Count -gt $g.Rows) "copied $($lines.Count) lines, no more than the $($g.Rows) rows on screen"
    $n = $lines.Count - 1
    $first = Assert-SelConsecutive -Lines $lines[0..($n - 1)]
    Assert-True ($first + $n - 1 -eq 300) "the numbers end at $($first + $n - 1), not 300"
    Assert-True ($lines[$n].Trim().Length -gt 0) "the last copied line is not the prompt"
    "copied $($lines.Count) lines ($first..300 and the prompt) with $($g.Rows) rows on screen"
}

Invoke-Case "select_drag_above_top_selects_first_row" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line "seq 1 300"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^300\s*$" -TimeoutSec 5) "seq output incomplete"
    Start-Sleep -Milliseconds 500
    $from  = Get-NutshellCellPoint -Geo $g -Row 3 -Col 6
    $above = Get-NutshellCellPoint -Geo $g -Row -2.5 -Col 0               # two cell heights above the terminal area
    Send-NutshellDrag -Session $s -From $from -To $above
    # The log writer flushes debug lines every 250 ms; once sel.copy is on disk
    # every sel.scroll line before it is too.
    Assert-True (Wait-NutshellDebugLog -Session $s -Pattern 'sel\.copy ' -TimeoutSec 5) "the release logged no sel.copy"
    # Timer ticks between the sent messages may auto-scroll a few lines
    # before the release; the last sel.scroll says how many.
    $scrolls = [regex]::Matches((Get-NutshellDebugLogText -Session $s), 'sel\.scroll .*offset=(\d+)')
    $off = if ($scrolls.Count -gt 0) { [int]$scrolls[$scrolls.Count - 1].Groups[1].Value } else { 0 }
    $lines = @((Get-SelClipboard) -split "`r`n")
    Assert-True ($lines.Count -eq 4 + $off) "copied $($lines.Count) lines; expected rows 0-3 plus $off auto-scrolled"
    $first = Assert-SelConsecutive -Lines $lines
    "copied $first..$($first + $lines.Count - 1), auto-scrolled $off"
}

Invoke-Case "select_while_scrolled_back_copies_highlight" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line "seq 1 300"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^300\s*$" -TimeoutSec 5) "seq output incomplete"
    Start-Sleep -Milliseconds 500
    $a = Get-NutshellCellPoint -Geo $g -Row 0 -Col 0
    $b = Get-NutshellCellPoint -Geo $g -Row 2 -Col 6
    Send-NutshellDrag -Session $s -From $a -To $b
    $live = @((Get-SelClipboard) -split "`r`n")
    Assert-True ($live.Count -eq 3) "the live drag copied $($live.Count) lines"
    $liveFirst = Assert-SelConsecutive -Lines $live
    Send-NutshellWheel -Session $s -Lines 100
    Send-NutshellDrag -Session $s -From $a -To $b
    $back = @((Get-SelClipboard) -split "`r`n")
    Assert-True ($back.Count -eq 3) "the scrolled-back drag copied $($back.Count) lines"
    $backFirst = Assert-SelConsecutive -Lines $back
    Assert-True ($backFirst -eq $liveFirst - 100) "scrolled back 100 lines, the drag copied from $backFirst, not $($liveFirst - 100)"
    "rows 0-2 hold $liveFirst.. live and $backFirst.. scrolled back"
}

Invoke-Case "click_does_not_touch_clipboard" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line 'echo CLICK$((3+4))TEXT'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "CLICK7TEXT" -TimeoutSec 5) "echo output missing"
    # A real selection first, so the click has one to clear.
    Send-NutshellDrag -Session $s -From (Get-NutshellCellPoint -Geo $g -Row 1 -Col 0) -To (Get-NutshellCellPoint -Geo $g -Row 1 -Col 12)
    Assert-True ((Get-SelClipboard) -eq "CLICK7TEXT") "the setup drag did not copy: '$(Get-SelClipboard)'"
    Set-Clipboard -Value "CLICK_MARKER_5"
    # Press on the last pixel of cell (1,2) and release half a drag threshold
    # to the right: across a cell boundary, under the threshold.
    $cx = [NutshellSelNative]::GetSystemMetrics(68)                  # SM_CXDRAG
    $half = [Math]::Max(1, [int][Math]::Floor(($cx - 1) / 2))
    $px = $g.X + 3 * $g.CharW - 1
    $py = [int]($g.Y + 1.5 * $g.CharH)
    Send-NutshellDrag -Session $s -From @($px, $py) -To @(($px + $half), $py) -Steps 1
    Assert-True ((Get-SelClipboard) -eq "CLICK_MARKER_5") "a click under the drag threshold changed the clipboard"
    Send-NutshellChord -Session $s -Key C -Ctrl -Shift -SettleMs 400
    Assert-True ((Get-SelClipboard) -eq "CLICK_MARKER_5") "the click did not clear the selection: Ctrl+Shift+C copied it"
    "SM_CXDRAG $cx, moved $half px: clipboard untouched, selection cleared"
}

Invoke-Case "ctrl_shift_c_copies_after_drag" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line 'echo SEL$((2+3))TEXT'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "SEL5TEXT" -TimeoutSec 5) "echo output missing"
    Send-NutshellDrag -Session $s -From (Get-NutshellCellPoint -Geo $g -Row 1 -Col 0) -To (Get-NutshellCellPoint -Geo $g -Row 1 -Col 12)
    Assert-True ((Get-SelClipboard) -eq "SEL5TEXT") "the release did not copy: '$(Get-SelClipboard)'"
    Set-Clipboard -Value "COPY_MARKER_1"
    Send-NutshellChord -Session $s -Key C -Ctrl -Shift -SettleMs 400
    Assert-True ((Get-SelClipboard) -eq "SEL5TEXT") "Ctrl+Shift+C did not copy the selection again"
    Set-Clipboard -Value "COPY_MARKER_2"
    Send-NutshellCommand -Session $s -Id 2010 -SettleMs 400           # IDM_EDIT_COPY
    Assert-True ((Get-SelClipboard) -eq "SEL5TEXT") "Edit > Copy did not copy the selection"
    "release, Ctrl+Shift+C and Edit > Copy all copied the selection"
}

Invoke-Case "ctrl_c_with_selection_interrupts" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line 'echo SEL$((3+3))TEXT'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "SEL6TEXT" -TimeoutSec 5) "echo output missing"
    Send-NutshellDrag -Session $s -From (Get-NutshellCellPoint -Geo $g -Row 1 -Col 0) -To (Get-NutshellCellPoint -Geo $g -Row 1 -Col 12)
    Assert-True ((Get-SelClipboard) -eq "SEL6TEXT") "the release did not copy"
    Send-NutshellLine -Session $s -Line "sleep 30"
    Start-Sleep -Milliseconds 800
    Send-NutshellChord -Session $s -Key C -Ctrl -SettleMs 300
    Send-NutshellLine -Session $s -Line 'echo AFTER_$((4+4))'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "AFTER_8" -TimeoutSec 2) "the prompt did not return within 2 s: Ctrl+C with a selection was not sent as SIGINT"
    Assert-True ((Get-SelClipboard) -eq "SEL6TEXT") "Ctrl+C changed the clipboard"
    "sleep 30 interrupted with a selection on screen; clipboard kept"
}

Invoke-Case "double_click_selects_word" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    $style = [int64][NutshellSelNative]::GetClassLongPtr($s.Main, -26)   # GCL_STYLE
    Assert-True (($style -band 0x0008) -ne 0) ("the main window class lacks CS_DBLCLKS (style 0x{0:X})" -f $style)
    Send-NutshellLine -Session $s -Line "echo /usr/local/bin and more"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^/usr/local/bin and more" -TimeoutSec 5) "echo output missing"
    Set-Clipboard -Value "WORD_MARKER"
    $p = Get-NutshellCellPoint -Geo $g -Row 1 -Col 6
    Send-NutshellDrag -Session $s -From $p -To $p -Clicks 2 -Steps 1
    Assert-True ((Get-SelClipboard) -eq "/usr/local/bin") "double-click copied '$(Get-SelClipboard)'"
    "double-click copied /usr/local/bin; CS_DBLCLKS set"
}

Invoke-Case "triple_click_selects_line" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line "echo /usr/local/bin and more"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^/usr/local/bin and more" -TimeoutSec 5) "echo output missing"
    Set-Clipboard -Value "LINE_MARKER"
    $p = Get-NutshellCellPoint -Geo $g -Row 1 -Col 6
    Send-NutshellDrag -Session $s -From $p -To $p -Clicks 3 -Steps 1
    Assert-True ((Get-SelClipboard) -eq "/usr/local/bin and more") "triple-click copied '$(Get-SelClipboard)'"
    "triple-click copied the line"
}

Invoke-Case "shift_click_extends_selection" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line "echo /usr/local/bin and more"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^/usr/local/bin and more" -TimeoutSec 5) "echo output missing"
    Send-NutshellDrag -Session $s -From (Get-NutshellCellPoint -Geo $g -Row 1 -Col 0) -To (Get-NutshellCellPoint -Geo $g -Row 1 -Col 3)
    Assert-True ((Get-SelClipboard) -eq "/usr") "the setup drag copied '$(Get-SelClipboard)'"
    Set-Clipboard -Value "EXTEND_MARKER_1"
    $p = Get-NutshellCellPoint -Geo $g -Row 1 -Col 13
    Send-NutshellDrag -Session $s -From $p -To $p -Shift -Steps 1
    Assert-True ((Get-SelClipboard) -eq "/usr/local/bin") "Shift+click copied '$(Get-SelClipboard)'"
    Assert-True (Wait-NutshellDebugLog -Session $s -Pattern 'sel\.extend ' -TimeoutSec 4) "no sel.extend event for the Shift+click"
    "Shift+click extended /usr to /usr/local/bin"
}

Invoke-Case "middle_click_extends_selection" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line "echo /usr/local/bin and more"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^/usr/local/bin and more" -TimeoutSec 5) "echo output missing"
    Send-NutshellDrag -Session $s -From (Get-NutshellCellPoint -Geo $g -Row 1 -Col 0) -To (Get-NutshellCellPoint -Geo $g -Row 1 -Col 3)
    Assert-True ((Get-SelClipboard) -eq "/usr") "the setup drag copied '$(Get-SelClipboard)'"
    Set-Clipboard -Value "EXTEND_MARKER_2"
    $p = Get-NutshellCellPoint -Geo $g -Row 1 -Col 13
    Send-NutshellDrag -Session $s -From $p -To $p -Button Middle -Steps 1
    Assert-True ((Get-SelClipboard) -eq "/usr/local/bin") "middle-click copied '$(Get-SelClipboard)'"
    Assert-True (Wait-NutshellDebugLog -Session $s -Pattern 'sel\.extend ' -TimeoutSec 4) "no sel.extend event for the middle-click"
    "middle-click extended /usr to /usr/local/bin"
}

Invoke-Case "fourth_click_starts_over" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    Send-NutshellLine -Session $s -Line "echo /usr/local/bin and more"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^/usr/local/bin and more" -TimeoutSec 5) "echo output missing"
    Set-Clipboard -Value "CYCLE_MARKER"
    # Four rapid clicks, the fourth held and dragged from col 4 to col 8: the
    # cycle is back at a single cell, so exactly those cells are copied.
    Send-NutshellDrag -Session $s -From (Get-NutshellCellPoint -Geo $g -Row 1 -Col 4) `
        -To (Get-NutshellCellPoint -Geo $g -Row 1 -Col 8) -Clicks 4
    Assert-True ((Get-SelClipboard) -eq "/loca") "the fourth click copied '$(Get-SelClipboard)', expected /loca (a cell drag, not the line)"
    "a fourth rapid click started a cell selection again"
}

Invoke-Case "double_click_selects_accented_word" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    # Only the mark is escaped, so the command stays short enough not to wrap on a long prompt.
    Send-NutshellLine -Session $s -Line "printf 'cafe\xcc\x81 noir\n'"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^cafe\S* noir" -TimeoutSec 5) "printf output missing"
    Set-Clipboard -Value "ACCENT_MARKER"
    $p = Get-NutshellCellPoint -Geo $g -Row 1 -Col 3                  # the e; its mark is col 4
    Send-NutshellDrag -Session $s -From $p -To $p -Clicks 2 -Steps 1
    $got = [Text.Encoding]::UTF8.GetBytes((Get-SelClipboard))
    $want = [byte[]](0x63, 0x61, 0x66, 0x65, 0xCC, 0x81)             # cafe + U+0301
    Assert-True (($got -join ",") -eq ($want -join ",")) "double-click copied bytes $($got -join ' '), expected $($want -join ' ')"
    "double-click on the e copied the whole decomposed word, mark included"
}

Invoke-Case "output_into_selection_clears_it" $script:SelSettings {
    param($s)
    $g = Start-SelCase $s
    # A short command name keeps the command on row 0 and its output on row 1.
    Send-NutshellLine -Session $s -Line 'f(){ for i in 1 2 3 4 5 6; do printf "\r$i"; sleep 1; done; echo; }'
    Send-NutshellLine -Session $s -Line "clear" -SettleMs 800
    Send-NutshellLine -Session $s -Line "f" -SettleMs 1200
    Send-NutshellDrag -Session $s -From (Get-NutshellCellPoint -Geo $g -Row 1 -Col 0) -To (Get-NutshellCellPoint -Geo $g -Row 1 -Col 4)
    Assert-True (Wait-NutshellDebugLog -Session $s -Pattern 'sel\.clear .*reason="?stale' -TimeoutSec 4) "no sel.clear reason=stale after the loop wrote into the selected row"
    Set-Clipboard -Value "STALE_MARKER_9"
    Send-NutshellChord -Session $s -Key C -Ctrl -Shift -SettleMs 400
    Assert-True ((Get-SelClipboard) -eq "STALE_MARKER_9") "Ctrl+Shift+C copied a selection the output had overwritten"
    "the next write cleared the selection; Ctrl+Shift+C copied nothing"
}
