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
    # Sentinel first: only tab A's session log is being watched ($s.Log was
    # captured before tab B existed), so if the switch back did not take
    # effect this line goes to tab B and nothing arrives -- which is a
    # different fault from "the resize never reached the inactive tab".
    Send-NutshellLine -Session $s -Line 'echo BACK_ON_TAB_A'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "BACK_ON_TAB_A" -TimeoutSec 5) "the switch back to tab A did not take effect: typing still reached the other tab"
    Send-NutshellLine -Session $s -Line 'echo A_LINES=$(tput lines)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "A_LINES=(\d+)" -TimeoutSec 5) "no size report from tab A"
    $a = [int][regex]::Match((Get-NutshellLogText -Session $s), "A_LINES=(\d+)").Groups[1].Value
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "inactive_tab_after_resize.png") | Out-Null
    Assert-True ($a -ge 30) "tab A still has the pre-resize grid: tput lines = $a (expected >= 30 for a 1300px-tall window)"
    "tab A reports $a lines after the resize happened on tab B"
}
