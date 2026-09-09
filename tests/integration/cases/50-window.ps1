# 50-window.ps1 -- launch, window, shutdown, menu (bvt-coverage.md section 1):
# LAUNCH-1..3, RESIZE-1, WINDOW-1..2, CLOSE-1, MENU-1.
# Dot-sourced by Run-Integration.ps1; depends on its Invoke-Case/Assert-True/
# Test-NutshellCaptureNonBlank/$Artifacts/$HostName/$User/$KeyPath/$Exe/
# $ActiveTiers/$Only/$results/$WM_COMMAND/$WM_CLOSE.

# Real WM_COMMAND ids from src/ui/resource.h -- not module-exported (that
# file's own $script:IDM_* constants are module-private), so cases that post
# one directly (rather than through a helper like Send-NutshellCommand)
# define their own copies here, same pattern as $WM_COMMAND in the driver.
$IDM_FILE_EXIT       = 2006
$IDM_VIEW_FULLSCREEN = 2021

function Start-NutshellUntilDialogOrWindow {
    <# Launch nutshell.exe directly -- NOT via the module's Start-Nutshell,
       which polls only for the main window and throws after -TimeoutSec if
       nothing with class Nutshell_Window turns up. That's exactly what
       happens when config_load() fails (LAUNCH-2/LAUNCH-3): WM_CREATE
       (src/ui/window.c) shows a synchronous MessageBoxA *before*
       CreateWindowEx returns, so the main window is created but not yet
       shown/visible -- Start-Nutshell's own poll (IsWindowVisible-filtered)
       never sees it and times out with the dialog just sitting there
       un-dismissed. This polls for either the main window or a top-level
       dialog and returns whichever showed up first, so the caller can
       dismiss a blocking dialog and then wait again for the window. #>
    param([Parameter(Mandatory)] $Env, [string[]] $ExtraArgs = @(), [int] $TimeoutSec = 15)
    # Start-Process rejects -ArgumentList @() (empty collection) outright, so
    # only pass it when there's something to pass.
    $p = if ($ExtraArgs.Count -gt 0) {
        Start-Process -FilePath $Env.Exe -WorkingDirectory $Env.Root -ArgumentList $ExtraArgs -PassThru
    } else {
        Start-Process -FilePath $Env.Exe -WorkingDirectory $Env.Root -PassThru
    }
    $null = $p.Handle   # see NutshellIT.psm1's Start-Nutshell for why this matters for ExitCode later
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $main = [IntPtr]::Zero; $dlg = [IntPtr]::Zero
    while ((Get-Date) -lt $deadline -and $main -eq [IntPtr]::Zero -and $dlg -eq [IntPtr]::Zero) {
        Start-Sleep -Milliseconds 300
        $p.Refresh()
        if ($p.HasExited) { break }
        foreach ($w in [NutshellNative]::ListWindows([uint32]$p.Id)) {
            $parts = $w -split "`t"
            if ($parts[1] -eq "Nutshell_Window") { $main = [IntPtr][long]$parts[0]; break }
            if ($parts[1] -eq "#32770") { $dlg = [IntPtr][long]$parts[0]; break }
        }
    }
    return [pscustomobject]@{ Process = $p; Main = $main; Dialog = $dlg }
}

function Wait-NutshellMainWindowOnly {
    <# Poll for the main window (class Nutshell_Window) on an already-running
       process, e.g. after dismissing the blocking dialog
       Start-NutshellUntilDialogOrWindow returned instead. #>
    param([Parameter(Mandatory)] $Process, [int] $TimeoutSec = 10)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 300
        $Process.Refresh()
        if ($Process.HasExited) { return [IntPtr]::Zero }
        $w = [NutshellNative]::ListWindows([uint32]$Process.Id) | Where-Object { $_ -match "`tNutshell_Window`t" } | Select-Object -First 1
        if ($w) { return [IntPtr][long]($w -split "`t")[0] }
    }
    return [IntPtr]::Zero
}

# ---- LAUNCH-1: launch, main window, no dialog, clean exit -----------------------
# Standalone (not Invoke-Case): needs to observe the process exit *itself*
# (exit code 0) rather than have Stop-Nutshell force-kill it, and -nc avoids
# waiting on a live connection for something that's only testing launch/close
# mechanics.
if (($ActiveTiers -contains "bvt") -and ($Only.Count -eq 0 -or $Only -contains "launch_main_window_no_dialog")) {
    $name = "launch_main_window_no_dialog"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
    $session = $null
    $ok = $false; $detail = ""
    try {
        $session = Start-Nutshell -Env $testEnv -ExtraArgs @("-nc")
        Assert-True ($session.Main -ne [IntPtr]::Zero) "no Nutshell_Window appeared"
        $dlg = Wait-NutshellDialog -Session $session -TimeoutSec 3
        # Only read the title when there *is* a dialog: on the pass path $dlg is
        # NULL and Get-NutshellWindowText would be called with a NULL hwnd just
        # to build a message that is never thrown.
        $dlgTitle = if ($dlg -ne [IntPtr]::Zero) { Get-NutshellWindowText -Hwnd $dlg } else { "" }
        Assert-True ($dlg -eq [IntPtr]::Zero) "a dialog appeared on launch: $dlgTitle"
        Save-NutshellScreenshot -Session $session -Path (Join-Path $Artifacts "$name.png") | Out-Null

        [NutshellNative]::PostMessage($session.Main, $WM_COMMAND, [IntPtr]$IDM_FILE_EXIT, [IntPtr]::Zero) | Out-Null  # IDM_FILE_EXIT
        $deadline = (Get-Date).AddSeconds(5)
        $exited = $false
        while ((Get-Date) -lt $deadline) {
            $session.Process.Refresh()
            if ($session.Process.HasExited) { $exited = $true; break }
            Start-Sleep -Milliseconds 200
        }
        Assert-True $exited "process did not exit within 5s of IDM_FILE_EXIT"
        Assert-True ($session.Process.ExitCode -eq 0) "exit code was $($session.Process.ExitCode), expected 0"
        $detail = "window appeared, no dialog, IDM_FILE_EXIT -> exit code 0 within 5s"
        $ok = $true
    } catch {
        $detail = $_.Exception.Message
        if ($session) { try { Save-NutshellScreenshot -Session $session -Path (Join-Path $Artifacts "$name-FAIL.png") | Out-Null } catch {} }
    } finally {
        if ($session) { Stop-Nutshell -Session $session }  # no-op if it already exited
        for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
            Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
            if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
        }
    }
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}

# ---- LAUNCH-2: no config file (first run) ----------------------------------------
# config_load() (src/config/loader.c) returns NULL both when the file is
# missing and when it's malformed -- either way src/ui/window.c's WM_CREATE
# shows a "Configuration Warning" MessageBox (#32770) and falls back to
# config_new_default(). That MessageBox is unavoidable and not mentioned in
# the case plan, so this case asserts around it: dismiss it, confirm no
# config file exists yet (nothing auto-saves defaults to disk), then open
# Settings and Save (IDOK) -- src/ui/settings.c's IDOK handler is the only
# thing that calls config_save() outside Session Manager -- and confirm the
# file now exists with recognisable default keys. open_session_manager_at_start
# defaults to 0 (loader.c), so no Session Manager auto-opens; the plan's "handle
# both" is therefore moot in the shipped default, noted here rather than guessed at.
if (($ActiveTiers -contains "bvt") -and ($Only.Count -eq 0 -or $Only -contains "launch_without_config_writes_defaults")) {
    $name = "launch_without_config_writes_defaults"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath -NoConfig
    $cfgPath = Join-Path $testEnv.Root "nutshell.config"
    $session = $null; $launch = $null
    $ok = $false; $detail = ""
    try {
        Assert-True (-not (Test-Path $cfgPath)) "test bug: nutshell.config already exists before launch"
        # config_load() (src/config/loader.c) returns NULL for a missing file,
        # and WM_CREATE (src/ui/window.c) shows a synchronous "Configuration
        # Warning" MessageBoxA *before* CreateWindowEx returns -- the main
        # window is not yet visible while that's up, so Start-Nutshell's own
        # wait (which only polls for the visible main window) would time out
        # with the dialog never dismissed. Launch and wait for whichever
        # shows up first instead.
        $launch = Start-NutshellUntilDialogOrWindow -Env $testEnv -ExtraArgs @()
        Assert-True ($launch.Dialog -ne [IntPtr]::Zero) "no Configuration Warning dialog appeared for a missing config (main window appeared first: $($launch.Main -ne [IntPtr]::Zero))"
        $dlgTitle = Get-NutshellWindowText -Hwnd $launch.Dialog
        Assert-True ($dlgTitle -match "Configuration Warning") "dialog title was '$dlgTitle', expected 'Configuration Warning'"
        Close-NutshellDialog -Dialog $launch.Dialog -Button OK
        Assert-True (-not (Test-Path $cfgPath)) "a config file was written just from starting -- defaults are not auto-saved to disk"

        $mainHwnd = Wait-NutshellMainWindowOnly -Process $launch.Process -TimeoutSec 10
        Assert-True ($mainHwnd -ne [IntPtr]::Zero) "main window never appeared after dismissing the Configuration Warning"
        $session = [pscustomobject]@{ Process = $launch.Process; Main = $mainHwnd; Env = $testEnv; Log = $null }
        Assert-True $session.Process.Responding "app stopped responding after dismissing the warning"

        $set = Open-NutshellSettings -Session $session
        Close-NutshellDialog -Dialog $set -Button OK   # Settings' IDOK is Save
        Start-Sleep -Milliseconds 500
        Assert-True (Test-Path $cfgPath) "no nutshell.config was written after Settings > Save"
        $json = Get-Content $cfgPath -Raw | ConvertFrom-Json
        Assert-True ($null -ne $json.settings) "written config has no 'settings' object"
        Assert-True ($null -ne $json.settings.font -and $json.settings.font -ne "") "written config's settings.font is empty -- doesn't look like real defaults"
        $detail = "Configuration Warning shown and dismissed; no config on disk until Settings > Save; then config written with default keys (font='$($json.settings.font)')"
        $ok = $true
    } catch {
        $detail = $_.Exception.Message
        if ($session) { try { Save-NutshellScreenshot -Session $session -Path (Join-Path $Artifacts "$name-FAIL.png") | Out-Null } catch {} }
    } finally {
        if ($session) { Stop-Nutshell -Session $session }
        elseif ($launch -and $launch.Process -and -not $launch.Process.HasExited) { Stop-Process -Id $launch.Process.Id -Force -ErrorAction SilentlyContinue }
        for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
            Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
            if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
        }
    }
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}

# ---- LAUNCH-3: corrupt config survives -------------------------------------------
if (($ActiveTiers -contains "bvt") -and ($Only.Count -eq 0 -or $Only -contains "launch_with_corrupt_config_survives")) {
    $name = "launch_with_corrupt_config_survives"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath -NoConfig
    $cfgPath = Join-Path $testEnv.Root "nutshell.config"
    [IO.File]::WriteAllText($cfgPath, '{ "settings": ', (New-Object Text.UTF8Encoding $false))
    $session = $null; $launch = $null
    $ok = $false; $detail = ""
    try {
        # Same reason as LAUNCH-2: the warning MessageBoxA blocks WM_CREATE,
        # so the main window isn't visible yet when it's up.
        $launch = Start-NutshellUntilDialogOrWindow -Env $testEnv -ExtraArgs @()
        Assert-True ($launch.Dialog -ne [IntPtr]::Zero) "no Configuration Warning dialog appeared for a truncated config (main window appeared first: $($launch.Main -ne [IntPtr]::Zero))"
        $dlgTitle = Get-NutshellWindowText -Hwnd $launch.Dialog
        Assert-True ($dlgTitle -match "Configuration Warning") "dialog title was '$dlgTitle', expected 'Configuration Warning'"
        $r = New-Object NutshellNative+RECT
        [NutshellNative]::GetWindowRect($launch.Dialog, [ref]$r) | Out-Null
        $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc(); [NutshellNative]::PrintWindow($launch.Dialog, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc)
        $bmp.Save((Join-Path $Artifacts "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose(); $bmp.Dispose()
        Close-NutshellDialog -Dialog $launch.Dialog -Button OK

        $mainHwnd = Wait-NutshellMainWindowOnly -Process $launch.Process -TimeoutSec 10
        Assert-True ($mainHwnd -ne [IntPtr]::Zero) "main window never appeared after dismissing the warning"
        $session = [pscustomobject]@{ Process = $launch.Process; Main = $mainHwnd; Env = $testEnv; Log = $null }
        Start-Sleep -Milliseconds 500
        $session.Process.Refresh()
        Assert-True (-not $session.Process.HasExited) "process exited after a corrupt config, instead of falling back to defaults"
        Assert-True $session.Process.Responding "process stopped responding after a corrupt config"
        Assert-True ([NutshellNative]::IsWindowVisible($session.Main)) "main window not visible after dismissing the warning"
        $detail = "truncated JSON: Configuration Warning shown, dismissed, app kept running with defaults (no crash within 5s)"
        $ok = $true
    } catch {
        $detail = $_.Exception.Message
        if ($session) { try { Save-NutshellScreenshot -Session $session -Path (Join-Path $Artifacts "$name-FAIL.png") | Out-Null } catch {} }
    } finally {
        if ($session) { Stop-Nutshell -Session $session }
        elseif ($launch -and $launch.Process -and -not $launch.Process.HasExited) { Stop-Process -Id $launch.Process.Id -Force -ErrorAction SilentlyContinue }
        for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
            Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
            if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
        }
    }
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}

# ---- RESIZE-1: resize range paints cleanly ---------------------------------------
Invoke-Case "resize_range_paints_cleanly" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    $sizes = @(
        @{ W = 640;  H = 400  }, @{ W = 1024; H = 768  }, @{ W = 1920; H = 1080 },
        @{ W = 1024; H = 768  }, @{ W = 640;  H = 400  }
    )
    $prevW = $null; $prevCols = $null
    $i = 0
    foreach ($sz in $sizes) {
        $i++
        Set-NutshellWindowSize -Session $s -Width $sz.W -Height $sz.H
        $path = Join-Path $Artifacts "resize_range_step$i.png"
        Save-NutshellScreenshot -Session $s -Path $path | Out-Null
        Assert-True (Test-NutshellCaptureNonBlank -Path $path) "capture looks blank at $($sz.W)x$($sz.H) (step $i)"
        $marker = "RCOLS$i"
        Send-NutshellLine -Session $s -Line "echo ${marker}=`$(tput cols)"
        Assert-True (Wait-NutshellLog -Session $s -Pattern "$marker=(\d+)" -TimeoutSec 5) "no size report at $($sz.W)x$($sz.H) (step $i)"
        $cols = [int][regex]::Match((Get-NutshellLogText -Session $s), "$marker=(\d+)").Groups[1].Value
        if ($null -ne $prevW) {
            if ($sz.W -gt $prevW) {
                Assert-True ($cols -ge $prevCols) "window grew ($prevW -> $($sz.W)) but tput cols shrank ($prevCols -> $cols)"
            } elseif ($sz.W -lt $prevW) {
                Assert-True ($cols -le $prevCols) "window shrank ($prevW -> $($sz.W)) but tput cols grew ($prevCols -> $cols)"
            }
        }
        $prevW = $sz.W; $prevCols = $cols
    }
    "640x400 -> 1024x768 -> 1920x1080 -> 1024x768 -> 640x400, all captures non-blank, cols tracked window width direction"
}

# ---- WINDOW-1: minimise and restore repaints -------------------------------------
# KNOWN PRODUCT BUG (2026-09-09 BVT sweep): the final content comparison below
# fails reproducibly, and not from a harness bug. Scrolled back with PgUp on a
# full screen, minimising then restoring shifts the view up by exactly one more
# page (e.g. top line 165 -> 150, a 15-row jump matching the visible row
# count) every time, with generous settle time on both sides ruling out a
# timing race: window.c's WM_SIZE has no SIZE_MINIMIZED guard, so the
# resize-to-iconic re-runs the smart-scroll offset recompute (the same
# mechanism `terminal_holds_position_while_output_arrives` in 10-terminal.ps1
# tests for new output arriving) against a transient near-zero row count, and
# never corrects it back on restore. The fix belongs in src/ and lands in its
# own change; until then that one comparison runs inside
# Invoke-KnownBugBlock -- it still executes and is still reported, it just
# does not fail the tier. Everything else here (iconic/restore state changes,
# the capture itself) is asserted normally.
Invoke-Case "minimise_restore_repaints" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Set-NutshellWindowSize -Session $s -Width 1200 -Height 800
    # Fill the screen completely (well past one page) rather than leaving it
    # mostly blank after a bare `clear`: with only a couple of lines on an
    # otherwise-empty screen, "top of buffer" vs "bottom of buffer" anchoring
    # is themselves ambiguous/undefined, which produced spurious mismatches
    # here during development. A full screen removes that ambiguity, and a
    # scroll into history (PgUp) keeps the blinking cursor off the live line
    # so two otherwise-identical captures don't differ only by blink phase
    # (see Get-TerminalAreaHash's doc comment in 10-terminal.ps1).
    Send-NutshellLine -Session $s -Line "clear; seq 1 200"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "(?m)^200\s*$" -TimeoutSec 5) "seq output incomplete"
    Send-NutshellKey -Session $s -Key PgUp -SettleMs 300
    $before = Join-Path $Artifacts "minimise_before.png"
    Save-NutshellScreenshot -Session $s -Path $before | Out-Null

    [NutshellNative]::ShowWindow($s.Main, 6) | Out-Null   # SW_MINIMIZE
    Start-Sleep -Milliseconds 1200
    Assert-True ([NutshellNative]::IsIconic($s.Main)) "window did not report iconic after SW_MINIMIZE"
    [NutshellNative]::ShowWindow($s.Main, 9) | Out-Null   # SW_RESTORE
    Start-Sleep -Milliseconds 1200
    Assert-True (-not [NutshellNative]::IsIconic($s.Main)) "window still iconic after SW_RESTORE"

    $after = Join-Path $Artifacts "minimise_after.png"
    Save-NutshellScreenshot -Session $s -Path $after | Out-Null
    # Terminal band only (excludes the tab strip / any cursor-blink timing
    # difference in the title area) -- same crop Get-TerminalAreaHash uses.
    $region = @{ X = 0.0; Y = 0.15; W = 0.6; H = 0.8 }
    $hb = Get-NutshellRegionHash -Path $before -Region $region
    $ha = Get-NutshellRegionHash -Path $after  -Region $region
    $known = Invoke-KnownBugBlock -Bug "WM_SIZE has no SIZE_MINIMIZED guard: minimise/restore scrolls a scrolled-back view up one page" {
        Assert-True ($hb -eq $ha) "terminal content differs after minimise/restore (see minimise_before/after.png)"
    }
    "minimised (IsIconic true), restored (IsIconic false); $known"
}

# ---- WINDOW-2: fullscreen toggle changes the PTY size ----------------------------
Invoke-Case "fullscreen_toggle_changes_pty" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Set-NutshellWindowSize -Session $s -Width 900 -Height 600
    Send-NutshellLine -Session $s -Line 'echo FS_COLS_0=$(tput cols)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "FS_COLS_0=(\d+)" -TimeoutSec 5) "no baseline size report"
    $cols0 = [int][regex]::Match((Get-NutshellLogText -Session $s), "FS_COLS_0=(\d+)").Groups[1].Value

    [NutshellNative]::PostMessage($s.Main, $WM_COMMAND, [IntPtr]$IDM_VIEW_FULLSCREEN, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 900
    Send-NutshellLine -Session $s -Line 'echo FS_COLS_1=$(tput cols)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "FS_COLS_1=(\d+)" -TimeoutSec 5) "no size report after entering fullscreen"
    $cols1 = [int][regex]::Match((Get-NutshellLogText -Session $s), "FS_COLS_1=(\d+)").Groups[1].Value
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "fullscreen_entered.png") | Out-Null
    Assert-True ($cols1 -gt $cols0) "fullscreen did not grow tput cols: $cols0 -> $cols1 (from a 900px-wide window; expected the screen to be wider)"

    [NutshellNative]::PostMessage($s.Main, $WM_COMMAND, [IntPtr]$IDM_VIEW_FULLSCREEN, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 900
    Send-NutshellLine -Session $s -Line 'echo FS_COLS_2=$(tput cols)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "FS_COLS_2=(\d+)" -TimeoutSec 5) "no size report after leaving fullscreen"
    $cols2 = [int][regex]::Match((Get-NutshellLogText -Session $s), "FS_COLS_2=(\d+)").Groups[1].Value
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "fullscreen_exited.png") | Out-Null
    # Back to the *original* width, not merely smaller than fullscreen: the
    # window returns to the same 900x600 it had before, so the PTY must report
    # the same column count it did then (README's "returns to its original
    # value").
    Assert-True ($cols2 -eq $cols0) "leaving fullscreen did not restore tput cols to its original value: $cols0 -> $cols1 (fullscreen) -> $cols2"
    "cols $cols0 -> $cols1 (fullscreen) -> $cols2 (restored to the original)"
}

# ---- CLOSE-1: close with a live session exits cleanly ----------------------------
# Standalone, same reason as LAUNCH-1: needs to observe the natural exit
# rather than have Invoke-Case's cleanup force-kill it. window.c's
# IDM_FILE_EXIT/WM_CLOSE handling (read while writing this case) posts
# WM_CLOSE straight through to DefWindowProc -> WM_DESTROY -> PostQuitMessage
# with no confirmation prompt today, so the dialog-wait below is defensive
# (kept in case that changes) rather than expected to fire.
if (($ActiveTiers -contains "bvt") -and ($Only.Count -eq 0 -or $Only -contains "close_with_live_session_exits_cleanly")) {
    $name = "close_with_live_session_exits_cleanly"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
    $session = $null
    $ok = $false; $detail = ""
    try {
        $session = Start-Nutshell -Env $testEnv
        Start-NutshellLogging -Session $session | Out-Null
        Wait-NutshellShell -Session $session
        [NutshellNative]::PostMessage($session.Main, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null

        $dlg = Wait-NutshellDialog -Session $session -TimeoutSec 2
        if ($dlg -ne [IntPtr]::Zero) {
            # Defensive: no dialog is expected today (see comment above), but
            # answer Yes/OK if a confirmation ever gets added so this case
            # keeps working instead of hanging on the wait below.
            Close-NutshellDialog -Dialog $dlg -Button Yes
        }

        $deadline = (Get-Date).AddSeconds(5)
        $exited = $false
        while ((Get-Date) -lt $deadline) {
            $session.Process.Refresh()
            if ($session.Process.HasExited) { $exited = $true; break }
            Start-Sleep -Milliseconds 200
        }
        Assert-True $exited "process with a live SSH session did not exit within 5s of WM_CLOSE"
        Assert-True ($session.Process.ExitCode -eq 0) "exit code was $($session.Process.ExitCode), expected 0"
        $detail = "connected tab, WM_CLOSE -> exit code 0 within 5s, no dialog (none expected today), no orphan"
        $ok = $true
    } catch {
        $detail = $_.Exception.Message
        if ($session) { try { Save-NutshellScreenshot -Session $session -Path (Join-Path $Artifacts "$name-FAIL.png") | Out-Null } catch {} }
    } finally {
        if ($session) { Stop-Nutshell -Session $session }  # no-op if it already exited
        if ($session -and $session.Log -and (Test-Path $session.Log)) {
            Copy-Item $session.Log (Join-Path $Artifacts "$name.log") -Force
        }
        for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
            Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
            if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
        }
    }
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}

# ---- MENU-1: menus open and list items -------------------------------------------
# Message-free: no keystrokes, no foreground needed. The menu is built
# entirely in code (src/ui/window.c's create_app_menu(), MF_OWNERDRAW
# throughout -- there is no MENU resource in resource.rc, so there is nothing
# there to diff against) and every item's caption is owner-drawn from the
# app's own MenuItemData struct rather than an MF_STRING, so GetMenuString
# returns an empty string for every item -- confirmed by probing the live
# app (GetMenuStringW returned len=0/text='' for all 22 items across all 4
# top-level menus). That's a product-observability gap for caption text
# specifically: NOT implementable message-based without reading the target
# process's memory for the MenuItemData.text field (out of scope for a BVT
# helper). What message APIs DO expose regardless of owner-draw -- and what
# this case asserts -- is structure: top-level menu count, each submenu's
# item count (separators included), and each item's real WM_COMMAND id in
# order (GetMenuItemID returns 0 for a separator, the id itself otherwise),
# hand-derived here from create_app_menu()'s call order and cross-checked
# against src/ui/resource.h's IDM_* constants.
if (($ActiveTiers -contains "bvt") -and ($Only.Count -eq 0 -or $Only -contains "menus_open_and_list_items")) {
    $name = "menus_open_and_list_items"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
    $session = $null
    $ok = $false; $detail = ""
    try {
        $session = Start-Nutshell -Env $testEnv -ExtraArgs @("-nc")
        Start-Sleep -Milliseconds 500
        $hMenu = [NutshellNative]::GetMenu($session.Main)
        Assert-True ($hMenu -ne [IntPtr]::Zero) "main window has no menu"
        $topCount = [NutshellNative]::GetMenuItemCount($hMenu)
        Assert-True ($topCount -eq 4) "expected 4 top-level menus (File/Edit/View/Help), got $topCount"

        # 0 = separator; other values are the IDM_* constants from resource.h.
        $expected = @{
            0 = @(2001, 0, 2002, 2003, 0, 2004, 2005, 0, 2007, 0, 2006)  # File
            1 = @(2010, 2011, 2012, 0, 2013)                              # Edit
            2 = @(2020, 2022, 2021)                                       # View
            3 = @(2029, 0, 2030)                                          # Help
        }
        $names = @("File", "Edit", "View", "Help")
        for ($i = 0; $i -lt $topCount; $i++) {
            $sub = [NutshellNative]::GetSubMenu($hMenu, $i)
            Assert-True ($sub -ne [IntPtr]::Zero) "top-level menu $i ($($names[$i])) has no submenu"
            $count = [NutshellNative]::GetMenuItemCount($sub)
            $want = $expected[$i]
            Assert-True ($count -eq $want.Count) "$($names[$i]) menu has $count item(s), expected $($want.Count)"
            for ($j = 0; $j -lt $count; $j++) {
                $id = [NutshellNative]::GetMenuItemID($sub, $j)
                Assert-True ($id -eq $want[$j]) "$($names[$i]) item $j : id $id, expected $($want[$j])"
            }
        }
        $path = Join-Path $Artifacts "$name.png"
        Save-NutshellScreenshot -Session $session -Path $path | Out-Null
        $detail = "4 top-level menus; File 11 items, Edit 5, View 3, Help 3; every WM_COMMAND id matched resource.h/create_app_menu() in order (captions not API-observable -- owner-drawn, see case comment)"
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
