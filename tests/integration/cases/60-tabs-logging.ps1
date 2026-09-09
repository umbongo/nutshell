# 60-tabs-logging.ps1 -- tabs (bvt-coverage.md section 4) and logging (section 5):
# TABS-1, TABS-2, LOG-1, LOG-2.
# Dot-sourced by Run-Integration.ps1; depends on its Invoke-Case/Assert-True/
# $Artifacts/$HostName/$User/$KeyPath/$Exe/$ActiveTiers/$Only/$results/
# $WM_COMMAND/$WM_CHAR.

function Find-NutshellColorInRegion {
    <# Scan a proportional region of a capture (step-2px, cheap) for the
       first pixel within -Tolerance of -Rgb. Used by tab_status_dot_colours
       instead of a single hardcoded pixel: tabs.c's status-dot geometry
       (INDICATOR_W_BASE/INDICATOR_GAP_BASE/TAB_START_X_S, all ns_scale'd)
       would need to be re-derived exactly to hit one pixel reliably across
       DPIs -- scanning the tab-strip band for the expected colour is far
       less fragile and still proves the dot is painted in that colour
       somewhere in the strip. Returns @{X;Y} or $null.

       Callers must pass the Y band from Get-NutshellTabStripRegion, which
       reads the real Nutshell_Tabs child window's rect. The old default band
       (0.10-0.22, "just under the title and menu bars of an 800px-tall
       window") was wrong twice over: Set-NutshellWindowSize clamps to the work
       area so the window is not necessarily the size that was asked for, and
       the chrome above the strip scales with the monitor's DPI -- on this dev
       box at 288 DPI the strip sits below 0.22 entirely, so the scan looked at
       the menu bar and found no status dot at all. The X band still defaults
       to the left 40% (the harness only ever opens one or two minimum-width
       tabs), so a match cannot come from the far side of the strip. #>
    param([Parameter(Mandatory)] [string] $Path, [int[]] $Rgb, [int] $Tolerance = 20,
          [double] $XMin = 0.0, [double] $XMax = 0.4,
          [Parameter(Mandatory)] [double] $YMin, [Parameter(Mandatory)] [double] $YMax, [int] $Step = 2)
    $bmp = New-Object System.Drawing.Bitmap $Path
    try {
        $x0 = [int]($bmp.Width * $XMin); $x1 = [int]($bmp.Width * $XMax)
        $y0 = [int]($bmp.Height * $YMin); $y1 = [int]($bmp.Height * $YMax)
        for ($y = $y0; $y -lt $y1; $y += $Step) {
            for ($x = $x0; $x -lt $x1; $x += $Step) {
                $c = $bmp.GetPixel($x, $y)
                if ([Math]::Abs([int]$c.R - $Rgb[0]) -le $Tolerance -and
                    [Math]::Abs([int]$c.G - $Rgb[1]) -le $Tolerance -and
                    [Math]::Abs([int]$c.B - $Rgb[2]) -le $Tolerance) {
                    return @{ X = $x; Y = $y }
                }
            }
        }
        return $null
    } finally { $bmp.Dispose() }
}

# ---- TABS-1: open, switch (implicitly), close, the other tab is active ----------
# Open-NutshellSecondTab is fully posted (Session Manager listbox selection +
# IDOK) so the tab it opens becomes active on its own. Closing it is then just
# Ctrl+W (WM_CHAR 0x17) on the *already*-active tab -- window.c's WM_CHAR
# handler closes whichever tab is active, so this whole case needs no
# Select-NutshellTab at all, unlike what the case plan assumed. Get-NutshellTabCount is not implementable (see
# its doc comment), so "the count drops" is shown via a tab-strip capture
# hash changing when the second tab opens, and "the other tab is active" via
# a marker landing in tab A's already-running log after the close.
#
# This case found a real product bug in the 2026-09-09 BVT sweep: after Ctrl+W
# closed tab B, the content pane showed the idle/no-session placeholder (the
# acorn watermark) instead of tab A's terminal, the tab strip still showed tab
# A as connected (green dot), but posted keystrokes reached no shell at all --
# not a stale-paint timing issue, since a follow-up resize did not bring tab
# A's content back either. tabs_remove never told window.c that a survivor had
# taken over the active slot, so on_tab_close left g_active_session NULL.
# Fixed in v1.1.13; every assertion here now runs normally.
Invoke-Case "tabs_open_switch_close" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null   # tab A's log -- stays $s.Log throughout
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line "echo TAB_A_ALPHA"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "TAB_A_ALPHA" -TimeoutSec 10) "tab A not ready"

    # A large window (clamped to the work area by the helper), then the strip's
    # band read off the real Nutshell_Tabs child window rather than guessed as
    # a fraction of it: at the default (unset) launch size the strip sat
    # outside a naively-picked "top 8%" band, and a fixed 0.10-0.22 band only
    # lands on the strip at one window size and one DPI (it misses entirely on
    # this dev box's 288-DPI session). Get-NutshellTabStripRegion is right on
    # any desktop.
    Set-NutshellWindowSize -Session $s -Width 1200 -Height 800 | Out-Null
    $stripRegion = Get-NutshellTabStripRegion -Session $s -Pad 0.005
    $before = Join-Path $Artifacts "tabs_strip_1open.png"
    Save-NutshellScreenshot -Session $s -Path $before | Out-Null

    Open-NutshellSecondTab -Session $s   # posted end to end; tab B connects and becomes active
    Assert-True (((Get-NutshellWindows -Session $s) | Where-Object { $_ -match "Session Manager" }).Count -eq 0) "Session Manager still open; Connect failed"
    Start-Sleep -Milliseconds 800
    $afterOpen = Join-Path $Artifacts "tabs_strip_2open.png"
    Save-NutshellScreenshot -Session $s -Path $afterOpen | Out-Null
    Assert-True ((Get-NutshellRegionHash -Path $before -Region $stripRegion) -ne (Get-NutshellRegionHash -Path $afterOpen -Region $stripRegion)) `
        "tab strip capture did not change after opening a second tab"

    [NutshellNative]::PostMessage($s.Main, $WM_CHAR, [IntPtr]0x17, [IntPtr]::Zero) | Out-Null  # Ctrl+W closes the active tab (B)
    Start-Sleep -Milliseconds 2500
    $afterClose = Join-Path $Artifacts "tabs_strip_3closed.png"
    Save-NutshellScreenshot -Session $s -Path $afterClose | Out-Null
    Assert-True (Test-NutshellCaptureNonBlank -Path $afterClose) "capture looks blank after closing tab B: $afterClose"

    # "Back to one tab" literally: the strip must hash the same as it did with
    # only tab A open, not merely be non-blank (which any strip at all passes).
    Assert-True ((Get-NutshellRegionHash -Path $afterClose -Region $stripRegion) -eq (Get-NutshellRegionHash -Path $before -Region $stripRegion)) `
        "tab strip after closing tab B does not match the one-tab strip from before it was opened"
    # And tab A must be *live* again, not just drawn as active: the survivor
    # has to receive keyboard input and its terminal must be the one shown.
    # (Regression guard for the tab-close bug this case found: tabs_remove
    # never fired on_select for the survivor, so g_active_session stayed NULL
    # -- green dot in the strip, idle placeholder in the pane, keystrokes
    # dropped. Fixed by tabmgr_remove reporting the reselect; see
    # tests/test_tabs.c's tabmgr_remove_* cases.)
    Send-NutshellLine -Session $s -Line "echo TAB_A_STILL_ACTIVE_AFTER_CLOSE"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "TAB_A_STILL_ACTIVE_AFTER_CLOSE" -TimeoutSec 10) `
        "marker typed after closing tab B never reached tab A's log -- tab A is not the active tab"
    "opened tab B (posted, no click), strip changed, Ctrl+W closed it, strip back to one tab, tab A still receives input"
}

# ---- TABS-2: status dot colours ---------------------------------------------------
# Three independent phases (each needs a different connection outcome, so
# each gets its own scratch env/session -- Invoke-Case's single env doesn't
# fit): connecting (unroutable host, stays yellow/warning), connected
# (tompi, green/success), and disconnected (kill -9 $$ on the shell itself,
# red/danger -- the shell dying is an error, so TAB_DISCONNECTED and nothing
# else; accepting text_dim too would have made the phase pass on the idle
# colour as well, which is not what TABS-2 is checking). Every phase fixes the
# window first (for a repeatable capture) and then asks
# Get-NutshellTabStripRegion where the strip really is, rather than assuming a
# band. Colours come from tabs.c's status_color(): TAB_CONNECTING ->
# tok->warning.base, TAB_CONNECTED -> tok->success.base, TAB_DISCONNECTED ->
# tok->danger.base, TAB_IDLE -> tok->text_dim -- all of which equal the flat
# ui_theme.c value Get-NutshellThemeColor reads (ThemeSurface.base is the
# unmodified input colour; confirmed in src/core/ui_theme.c's
# resolve_surface()). New-NutshellTestEnv's default colour_scheme is
# "Onyx Synapse".
#
# The window size is still fixed for repeatability, but the scan band is no
# longer derived from it: each phase asks Get-NutshellTabStripRegion where the
# strip actually is. The previous fixed 0.10-0.22 band made this case fail on
# main's own product code on a 288-DPI, 1280x720 session -- the connected
# phase's success dot sits at y=176-205 of an 800px capture (0.22-0.26), just
# below the band, while the connecting phase only passed because the warning
# colour also appears in the connecting animation's glow at y=170.
if (($ActiveTiers -contains "bvt") -and ($Only.Count -eq 0 -or $Only -contains "tab_status_dot_colours")) {
    $name = "tab_status_dot_colours"
    Write-Host ("[RUN ] " + $name)
    $theme = "Onyx Synapse"
    $ok = $false; $detail = ""
    $phaseDetail = New-Object System.Collections.ArrayList
    try {
        # Phase 1: connecting (unroutable address -> TCP SYN never answers).
        $env1 = New-NutshellTestEnv -Exe $Exe -HostName "10.255.255.1" -User $User -KeyPath $KeyPath
        $s1 = $null
        try {
            $s1 = Start-Nutshell -Env $env1
            Set-NutshellWindowSize -Session $s1 -Width 1200 -Height 800 | Out-Null   # a fixed, repeatable window (clamped to the work area)
            Start-Sleep -Seconds 2
            $band1 = Get-NutshellTabStripRegion -Session $s1 -Pad 0.005
            $path1 = Join-Path $Artifacts "tabs_dot_connecting.png"
            Save-NutshellScreenshot -Session $s1 -Path $path1 | Out-Null
            $warn = Get-NutshellThemeColor -Name $theme -Token "warning"
            $hit1 = Find-NutshellColorInRegion -Path $path1 -Rgb @($warn.R, $warn.G, $warn.B) -YMin $band1.Y -YMax ($band1.Y + $band1.H)
            Assert-True ($null -ne $hit1) "no pixel matching '$theme' warning rgb($($warn.R),$($warn.G),$($warn.B)) found in the tab strip while connecting"
            [void]$phaseDetail.Add("connecting: warning token rgb($($warn.R),$($warn.G),$($warn.B)) matched at ($($hit1.X),$($hit1.Y))")
        } finally {
            if ($s1) { Stop-Nutshell -Session $s1 }   # our own child; unroutable connect would otherwise hang well past ssh_timeout
            for ($try = 0; $try -lt 5 -and (Test-Path $env1.Root); $try++) {
                Remove-Item -Recurse -Force $env1.Root -ErrorAction SilentlyContinue
                if (Test-Path $env1.Root) { Start-Sleep -Milliseconds 400 }
            }
        }

        # Phase 2: connected.
        $env2 = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
        $s2 = $null
        try {
            $s2 = Start-Nutshell -Env $env2
            Set-NutshellWindowSize -Session $s2 -Width 1200 -Height 800 | Out-Null   # same fixed geometry as phase 1
            Start-NutshellLogging -Session $s2 | Out-Null
            Wait-NutshellShell -Session $s2
            $band2 = Get-NutshellTabStripRegion -Session $s2 -Pad 0.005
            $path2 = Join-Path $Artifacts "tabs_dot_connected.png"
            Save-NutshellScreenshot -Session $s2 -Path $path2 | Out-Null
            $succ = Get-NutshellThemeColor -Name $theme -Token "success"
            $hit2 = Find-NutshellColorInRegion -Path $path2 -Rgb @($succ.R, $succ.G, $succ.B) -YMin $band2.Y -YMax ($band2.Y + $band2.H)
            Assert-True ($null -ne $hit2) "no pixel matching '$theme' success rgb($($succ.R),$($succ.G),$($succ.B)) found in the tab strip once connected"
            [void]$phaseDetail.Add("connected: success token rgb($($succ.R),$($succ.G),$($succ.B)) matched at ($($hit2.X),$($hit2.Y))")

            # Phase 3: disconnected -- kill the shell's own process from inside itself.
            $line = 'kill -9 $$'   # single-quoted: PowerShell must not touch $$ (its own "last token" automatic variable)
            Send-NutshellLine -Session $s2 -Line $line -SettleMs 500
            Start-Sleep -Seconds 3
            $path3 = Join-Path $Artifacts "tabs_dot_disconnected.png"
            Save-NutshellScreenshot -Session $s2 -Path $path3 | Out-Null
            $danger = Get-NutshellThemeColor -Name $theme -Token "danger"
            $hit3d  = Find-NutshellColorInRegion -Path $path3 -Rgb @($danger.R, $danger.G, $danger.B) -YMin $band2.Y -YMax ($band2.Y + $band2.H)
            Assert-True ($null -ne $hit3d) `
                "no pixel matching '$theme' danger rgb($($danger.R),$($danger.G),$($danger.B)) found in the tab strip after kill -9 `$`$"
            [void]$phaseDetail.Add("disconnected: danger token rgb($($danger.R),$($danger.G),$($danger.B)) matched at ($($hit3d.X),$($hit3d.Y))")
        } finally {
            if ($s2) { Stop-Nutshell -Session $s2 }
            for ($try = 0; $try -lt 5 -and (Test-Path $env2.Root); $try++) {
                Remove-Item -Recurse -Force $env2.Root -ErrorAction SilentlyContinue
                if (Test-Path $env2.Root) { Start-Sleep -Milliseconds 400 }
            }
        }
        $detail = $phaseDetail -join "; "
        $ok = $true
    } catch {
        $detail = ($phaseDetail -join "; ") + " -- FAILED: " + $_.Exception.Message
    }
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}

# ---- LOG-1: stop then restart logging opens a new file ---------------------------
Invoke-Case "logging_stop_then_restart_new_file" @{} {
    param($s)
    $log1 = Start-NutshellLogging -Session $s
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line "echo BEFORE_STOP_MARKER"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "BEFORE_STOP_MARKER" -TimeoutSec 10) "logging did not capture output before stopping"

    Send-NutshellCommand -Session $s -Id 2005 -SettleMs 500   # IDM_FILE_LOG_STOP
    Send-NutshellLine -Session $s -Line "echo AFTER_STOP_MARKER"
    Start-Sleep -Seconds 2   # give the (now-unwanted) write a chance to land if stopping didn't actually work
    Assert-True ((Get-Content $log1 -Raw) -notmatch "AFTER_STOP_MARKER") "output written after Stop Logging still landed in the log file"

    $log2 = Start-NutshellLogging -Session $s   # IDM_FILE_LOG_START; also updates $s.Log to the new file
    Assert-True ($log2 -ne $log1) "restarting logging reused the same file instead of opening a new one"
    Send-NutshellLine -Session $s -Line "echo AFTER_RESTART_MARKER"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "AFTER_RESTART_MARKER" -TimeoutSec 10) "the new log file never received output after restarting logging"
    Assert-True ((Get-Content $log1 -Raw) -notmatch "AFTER_RESTART_MARKER") "the post-restart marker leaked into the old (stopped) log file"
    "stopped: AFTER_STOP_MARKER absent from $(Split-Path $log1 -Leaf); restarted: new file $(Split-Path $log2 -Leaf) has AFTER_RESTART_MARKER"
}

# ---- LOG-2: debug terminal log is written -----------------------------------------
# open_debug_log() (src/ui/window.c) writes <profile-name>-debug-<timestamp>.log
# in the *exe's own directory* (get_exe_dir()), not settings.log_dir -- so this
# looks in $s.Env.Root, not $s.Env.Logs. debug_log_chunk() (src/config/ssh_io.c)
# renders ESC (0x1B) as the literal text "ESC" and fflush()es every chunk, so no
# waiting for a clean shutdown is needed.
Invoke-Case "debug_terminal_log_written" @{ debug_terminal = $true } {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line 'printf "\033[1mBOLD\033[0m\n"'
    Start-Sleep -Seconds 2
    $pattern = Join-Path $s.Env.Root ($s.Env.ProfileName + "-debug-*.log")
    $found = $null
    $deadline = (Get-Date).AddSeconds(8)
    while ((Get-Date) -lt $deadline -and -not $found) {
        $found = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $found) { Start-Sleep -Milliseconds 300 }
    }
    Assert-True ($null -ne $found) "no debug log matching '$($s.Env.ProfileName)-debug-*.log' appeared in $($s.Env.Root)"
    $content = Get-Content $found.FullName -Raw
    # The exact sequence sent, not a bare "ESC" -- every coloured shell prompt
    # puts an "ESC" in the log, so that alone would pass without our own
    # printf ever being logged. debug_log_chunk() renders 0x1B as "ESC", so
    # "\033[1m" comes out as the literal text "ESC[1m", followed by "BOLD".
    Assert-True ($content -match "ESC\[1m") "debug log '$($found.Name)' exists but contains no 'ESC[1m' for the escape sequence sent"
    Assert-True ($content -match "BOLD") "debug log '$($found.Name)' contains 'ESC[1m' but not the 'BOLD' text that followed it"
    "debug log $($found.Name) written with the sent sequence rendered as literal 'ESC[1m' followed by BOLD"
}
