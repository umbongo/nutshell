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
    # evidence screenshots (before/after Page Up) for eyeballing.
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line "seq 1 300"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^300\s*$" -TimeoutSec 5) "seq output incomplete"
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "page_up_before.png") | Out-Null
    Send-NutshellKey -Session $s -Key PgUp -SettleMs 400
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
    Send-NutshellKey -Session $s -Key PgUp -SettleMs 250
    Send-NutshellKey -Session $s -Key PgUp -SettleMs 500
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
