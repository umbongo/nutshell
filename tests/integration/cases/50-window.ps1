# 50-window.ps1 -- launch, window, shutdown, menu (integration-coverage.md section 1):
# LAUNCH-1..4, RESIZE-1, WINDOW-1..2, CLOSE-1, MENU-1, and HELP-1 (the keyboard shortcuts sheet).
# Dot-sourced by Run-Integration.ps1; depends on its Invoke-Case/Assert-True/
# Test-NutshellCaptureNonBlank/$Artifacts/$HostName/$User/$KeyPath/$Exe/
# $ActiveTiers/$Only/$results/$WM_COMMAND/$WM_CLOSE.

# Real WM_COMMAND ids from src/ui/resource.h -- not module-exported (that
# file's own $script:IDM_* constants are module-private), so cases that post
# one directly (rather than through a helper like Send-NutshellCommand)
# define their own copies here, same pattern as $WM_COMMAND in the driver.
$IDM_FILE_EXIT       = 2006
$IDM_VIEW_FULLSCREEN = 2021
$IDM_EDIT_SETTINGS   = 2013

function Start-NutshellUntilDialogOrWindow {
    <# Launch nutshell.exe directly -- NOT via the module's Start-Nutshell,
       which polls only for the main window and throws after -TimeoutSec if
       nothing with class Nutshell_Window turns up. The launch notices
       (config_load() failures LAUNCH-2/LAUNCH-3, a first run, an upgrade) are
       themed Nutshell_Notice dialogs, queued in WM_CREATE and shown only at
       the top of WM_STARTUP_CONNECT, after the main window is visible; each
       is modal over it and sits there un-dismissed until the caller closes
       it. This polls for either the main window or a top-level dialog and
       returns whichever showed up first, so the caller can dismiss a
       blocking dialog and then wait again for the window. #>
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

function Get-NutshellNoticeText {
    <# Reads a Nutshell_Notice (src/ui/notice_dlg.h control ids: title 100,
       opening 101, closing 102, label i = 110 + i, value i = 140 + i) through
       its real STATIC children. Returns Title (the caption), Opening, Closing
       and Items (objects with Label and Value). #>
    param([Parameter(Mandatory)] $Dialog)
    $read = {
        param([int] $Id)
        $c = Get-NutshellControl -Dialog $Dialog -Id $Id
        if ($c -eq [IntPtr]::Zero) { return $null }
        return (Get-NutshellControlText -Control $c)
    }
    $items = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt 24; $i++) {
        $label = & $read (110 + $i)
        if ($null -eq $label) { break }
        [void]$items.Add([pscustomobject]@{ Label = $label; Value = (& $read (140 + $i)) })
    }
    return [pscustomobject]@{
        Title = (Get-NutshellWindowText -Hwnd $Dialog)
        Opening = (& $read 101); Closing = (& $read 102); Items = @($items)
    }
}

function Wait-NutshellNotice {
    <# The Nutshell_Notice a launch raises (shown at the top of
       WM_STARTUP_CONNECT, after the main window is up), by title regex. #>
    param([Parameter(Mandatory)] $Process, [Parameter(Mandatory)] [string] $Title, [int] $TimeoutSec = 10)
    $sess = [pscustomobject]@{ Process = $Process }
    return (Wait-NutshellDialog -Session $sess -Title $Title -TimeoutSec $TimeoutSec)
}

# ---- LAUNCH-1: launch, main window, no dialog, clean exit -----------------------
# Standalone (not Invoke-Case): needs to observe the process exit *itself*
# (exit code 0) rather than have Stop-Nutshell force-kill it, and -nc avoids
# waiting on a live connection for something that's only testing launch/close
# mechanics.
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "launch_main_window_no_dialog")) {
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
# A missing config is a first run: WM_CREATE starts from config_new_default(),
# adds the saved "Local shell" profile and saves at once, so the file exists
# as soon as the main window does, and the "Welcome to Nutshell <ver>" notice
# (Nutshell_Notice, shown at the top of WM_STARTUP_CONNECT) lists what starts
# off, the AI assistant among it. This case asserts the file while the notice
# is still open, the notice's text, then (OK) that Settings > Save (IDOK)
# still writes recognisable default keys. open_session_manager_at_start
# defaults to 0 (loader.c), so no Session Manager auto-opens.
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "launch_without_config_writes_defaults")) {
    $name = "launch_without_config_writes_defaults"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath -NoConfig
    $cfgPath = Join-Path $testEnv.Root "nutshell.config"
    $session = $null; $launch = $null
    $ok = $false; $detail = ""
    try {
        Assert-True (-not (Test-Path $cfgPath)) "test bug: nutshell.config already exists before launch"
        # Wait for whichever shows up first, so an unexpected message box is
        # reported rather than timing out behind it.
        $launch = Start-NutshellUntilDialogOrWindow -Env $testEnv -ExtraArgs @()
        if ($launch.Dialog -ne [IntPtr]::Zero) {
            $dlgTitle = Get-NutshellWindowText -Hwnd $launch.Dialog
            Close-NutshellDialog -Dialog $launch.Dialog -Button OK
            throw "a first run (no config) showed a dialog '$dlgTitle' -- a missing config is not an error"
        }

        $mainHwnd = Wait-NutshellMainWindowOnly -Process $launch.Process -TimeoutSec 10
        Assert-True ($mainHwnd -ne [IntPtr]::Zero) "main window never appeared on a first run"
        $session = [pscustomobject]@{ Process = $launch.Process; Main = $mainHwnd; Env = $testEnv; Log = $null }

        $notice = Wait-NutshellNotice -Process $session.Process -Title "Welcome to Nutshell"
        Assert-True ($notice -ne [IntPtr]::Zero) "no first-run 'Welcome to Nutshell' notice appeared"
        $text = Get-NutshellNoticeText -Dialog $notice
        Assert-True ($text.Title -match '^Welcome to Nutshell \d+\.\d+\.\d+$') "first-run notice title was '$($text.Title)'"
        Assert-True ($text.Opening -match "No configuration was found") "first-run notice opening was '$($text.Opening)'"
        $ai = @($text.Items | Where-Object { $_.Label -match "AI assistant" })
        Assert-True ($ai.Count -eq 1) "first-run notice lists $($ai.Count) 'AI assistant' items, expected 1"
        Assert-True ($ai[0].Value -eq "off until you choose to turn it on") "AI assistant value was '$($ai[0].Value)'"
        Assert-True (Test-Path $cfgPath) "no nutshell.config while the first-run notice is open -- the file should be saved before the notice shows"
        Close-NutshellDialog -Dialog $notice -Button OK
        $deadline = (Get-Date).AddSeconds(5)
        while ((Get-Date) -lt $deadline -and [NutshellNative]::IsWindow($notice)) { Start-Sleep -Milliseconds 100 }
        Assert-True (-not [NutshellNative]::IsWindow($notice)) "OK did not close the first-run notice"

        Assert-True $session.Process.Responding "app stopped responding on a first run"
        Start-Sleep -Milliseconds 500
        Assert-True (Test-Path $cfgPath) "no nutshell.config after a first run -- the Local shell profile should have been saved"
        $first = Get-Content $cfgPath -Raw | ConvertFrom-Json
        Assert-True ($null -ne $first.profiles -and @($first.profiles).Count -ge 1) "first-run config has no profiles -- the Local shell profile is missing"
        Assert-True ($first.settings.ai_enabled -eq $false) "first-run config does not hold ai_enabled = false"

        $set = Open-NutshellSettings -Session $session
        Close-NutshellDialog -Dialog $set -Button OK   # Settings' IDOK is Save
        Start-Sleep -Milliseconds 500
        Assert-True (Test-Path $cfgPath) "no nutshell.config was written after Settings > Save"
        $json = Get-Content $cfgPath -Raw | ConvertFrom-Json
        Assert-True ($null -ne $json.settings) "written config has no 'settings' object"
        Assert-True ($null -ne $json.settings.font -and $json.settings.font -ne "") "written config's settings.font is empty -- doesn't look like real defaults"
        $detail = "first run: file saved at start with the Local shell profile and ai_enabled false; Welcome notice listed AI assistant 'off until you choose to turn it on'; OK closed it; Settings > Save wrote default keys (font='$($json.settings.font)')"
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
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "launch_with_corrupt_config_survives")) {
    $name = "launch_with_corrupt_config_survives"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath -NoConfig
    $cfgPath = Join-Path $testEnv.Root "nutshell.config"
    [IO.File]::WriteAllText($cfgPath, '{ "settings": ', (New-Object Text.UTF8Encoding $false))
    $session = $null; $launch = $null
    $ok = $false; $detail = ""
    try {
        # The warning is a Nutshell_Notice shown at the top of
        # WM_STARTUP_CONNECT, over the visible main window.
        $launch = Start-NutshellUntilDialogOrWindow -Env $testEnv -ExtraArgs @()
        Assert-True ($launch.Dialog -eq [IntPtr]::Zero) "a message box appeared instead of the themed notice for a truncated config"
        $mainHwnd = Wait-NutshellMainWindowOnly -Process $launch.Process -TimeoutSec 10
        Assert-True ($mainHwnd -ne [IntPtr]::Zero) "main window never appeared for a truncated config"
        $noticeDlg = Wait-NutshellNotice -Process $launch.Process -Title "Your configuration couldn't be loaded"
        Assert-True ($noticeDlg -ne [IntPtr]::Zero) "no 'Your configuration couldn't be loaded' notice appeared for a truncated config"
        $launch.Dialog = $noticeDlg
        $text = Get-NutshellNoticeText -Dialog $noticeDlg
        $bad = @(Get-ChildItem -Path $testEnv.Root -Filter "nutshell.config.bad-*" -ErrorAction SilentlyContinue)
        Assert-True ($bad.Count -eq 1) "expected one nutshell.config.bad-* backup beside the exe, found $($bad.Count)"
        Assert-True ($text.Opening.Contains($bad[0].Name)) "the notice does not name the backup '$($bad[0].Name)': $($text.Opening)"
        $dlgTitle = $text.Title
        $r = New-Object NutshellNative+RECT
        [NutshellNative]::GetWindowRect($launch.Dialog, [ref]$r) | Out-Null
        $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc(); [NutshellNative]::PrintWindow($launch.Dialog, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc)
        $bmp.Save((Join-Path $Artifacts "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose(); $bmp.Dispose()
        Close-NutshellDialog -Dialog $launch.Dialog -Button OK

        $session = [pscustomobject]@{ Process = $launch.Process; Main = $mainHwnd; Env = $testEnv; Log = $null }
        Start-Sleep -Milliseconds 500
        $session.Process.Refresh()
        Assert-True (-not $session.Process.HasExited) "process exited after a corrupt config, instead of falling back to defaults"
        Assert-True $session.Process.Responding "process stopped responding after a corrupt config"
        Assert-True ([NutshellNative]::IsWindowVisible($session.Main)) "main window not visible after dismissing the warning"
        $detail = "truncated JSON: 'couldn't be loaded' notice shown (naming the .bad- backup), dismissed, app kept running with defaults (no crash within 5s)"
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

# ---- LAUNCH-4: an older config gains the new settings, with a notice -------------
# A complete config minus ai_enabled (what a file from before the switch
# looks like, with no API key): WM_CREATE adds the missing key, saves at once,
# and WM_STARTUP_CONNECT shows the upgrade notice ("Welcome to Nutshell <ver>")
# listing it. The file already holds "ai_enabled": false while the notice is
# open.
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "launch_upgrade_adds_fields")) {
    $name = "launch_upgrade_adds_fields"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
    $cfgPath = Join-Path $testEnv.Root "nutshell.config"
    $launch = $null; $session = $null
    $ok = $false; $detail = ""
    try {
        $old = Get-Content $cfgPath -Raw | ConvertFrom-Json
        Assert-True ($old.settings.PSObject.Properties.Name -contains "ai_enabled") "test bug: the generated config has no ai_enabled to remove"
        $old.settings.PSObject.Properties.Remove("ai_enabled")
        [IO.File]::WriteAllText($cfgPath, ($old | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding $false))

        $launch = Start-NutshellUntilDialogOrWindow -Env $testEnv -ExtraArgs @("-nc")
        Assert-True ($launch.Dialog -eq [IntPtr]::Zero) "a message box appeared on an upgrade launch"
        $mainHwnd = Wait-NutshellMainWindowOnly -Process $launch.Process -TimeoutSec 10
        Assert-True ($mainHwnd -ne [IntPtr]::Zero) "main window never appeared on an upgrade launch"
        $session = [pscustomobject]@{ Process = $launch.Process; Main = $mainHwnd; Env = $testEnv; Log = $null }

        $notice = Wait-NutshellNotice -Process $session.Process -Title "Welcome to Nutshell"
        Assert-True ($notice -ne [IntPtr]::Zero) "no upgrade 'Welcome to Nutshell' notice appeared"
        $text = Get-NutshellNoticeText -Dialog $notice
        Assert-True ($text.Opening -match "introduces new settings") "upgrade notice opening was '$($text.Opening)'"
        Assert-True ($text.Items.Count -eq 1) "upgrade notice lists $($text.Items.Count) items, expected 1 (only ai_enabled was removed)"
        Assert-True ($text.Items[0].Label -match "AI assistant") "upgrade notice item label was '$($text.Items[0].Label)'"
        Assert-True ($text.Items[0].Value -eq "off until you choose to turn it on") "upgrade notice value was '$($text.Items[0].Value)' (no API key was set)"

        $now = Get-Content $cfgPath -Raw
        Assert-True ($now -match '"ai_enabled":\s*false') "the config does not hold ai_enabled = false while the notice is open"
        Close-NutshellDialog -Dialog $notice -Button OK
        Start-Sleep -Milliseconds 500
        $session.Process.Refresh()
        Assert-True (-not $session.Process.HasExited) "process exited after the upgrade notice"
        Assert-True ([NutshellNative]::IsWindowEnabled($session.Main)) "the main window is still disabled after the notice closed"
        $detail = "config minus ai_enabled: file saved at once with ai_enabled false; upgrade notice listed 'AI assistant: off until you choose to turn it on'; OK closed it"
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
    # Small -> medium -> as large as this desktop allows -> medium -> small.
    # The middle and top sizes are fractions of the work area rather than the
    # old hardcoded 1024x768 / 1920x1080: Set-NutshellWindowSize clamps to the
    # work area, so on a 1280x720 logon session "1920x1080" and "1024x768"
    # would both arrive as the same window and the direction assertions would
    # be comparing sizes the run never actually had. The comparisons below use
    # the *achieved* client width for the same reason.
    $wa = Get-NutshellWorkArea
    $midW = [Math]::Max(640, [int]($wa.Width * 0.7)); $midH = [Math]::Max(400, [int]($wa.Height * 0.7))
    $sizes = @(
        @{ W = 640;   H = 400   }, @{ W = $midW;    H = $midH     }, @{ W = $wa.Width; H = $wa.Height },
        @{ W = $midW; H = $midH }, @{ W = 640;      H = 400       }
    )
    $prevW = $null; $prevCols = $null
    $steps = New-Object System.Collections.ArrayList
    $i = 0
    foreach ($sz in $sizes) {
        $i++
        $r = Set-NutshellWindowSize -Session $s -Width $sz.W -Height $sz.H
        $got = "$($r.ClientWidth)x$($r.ClientHeight)"
        [void]$steps.Add($got)
        $path = Join-Path $Artifacts "resize_range_step$i.png"
        Save-NutshellScreenshot -Session $s -Path $path | Out-Null
        Assert-True (Test-NutshellCaptureNonBlank -Path $path) "capture looks blank at client $got (step $i)"
        $marker = "RCOLS$i"
        Send-NutshellLine -Session $s -Line "echo ${marker}=`$(tput cols)"
        Assert-True (Wait-NutshellLog -Session $s -Pattern "$marker=(\d+)" -TimeoutSec 5) "no size report at client $got (step $i)"
        $cols = [int][regex]::Match((Get-NutshellLogText -Session $s), "$marker=(\d+)").Groups[1].Value
        if ($null -ne $prevW) {
            if ($r.ClientWidth -gt $prevW) {
                Assert-True ($cols -ge $prevCols) "client width grew ($prevW -> $($r.ClientWidth)) but tput cols shrank ($prevCols -> $cols)"
            } elseif ($r.ClientWidth -lt $prevW) {
                Assert-True ($cols -le $prevCols) "client width shrank ($prevW -> $($r.ClientWidth)) but tput cols grew ($prevCols -> $cols)"
            }
        }
        $prevW = $r.ClientWidth; $prevCols = $cols
    }
    "client sizes " + ($steps -join " -> ") + ", all captures non-blank, cols tracked window width direction"
}

# ---- WINDOW-1: minimise and restore repaints -------------------------------------
# This case found a real product bug in the 2026-09-09 integration sweep: scrolled back
# with PgUp on a full screen, minimising then restoring shifted the view up by
# exactly one more page (e.g. top line 165 -> 150, a 15-row jump matching the
# visible row count) every time. window.c's WM_SIZE had no SIZE_MINIMIZED
# guard, so the resize-to-iconic re-ran the smart-scroll offset recompute (the
# same mechanism `terminal_holds_position_while_output_arrives` in
# 10-terminal.ps1 tests for new output arriving) against a transient near-zero
# row count, and never corrected it back on restore. Fixed in v1.1.12 (PR #12);
# every assertion here now runs normally.
Invoke-Case "minimise_restore_repaints" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    # A large window (clamped to the work area by the helper) -- nothing here
    # depends on the exact size, only on the same size before and after.
    Set-NutshellWindowSize -Session $s -Width 1200 -Height 800 | Out-Null
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
    # Plain PgUp on the primary screen no longer reaches the shell (it only
    # scrolls local history; see cases\90-keys.ps1's
    # pgup_pages_on_alt_screen_and_scrolls_on_primary), so Shift+PgUp, which
    # always scrolls history, is what drives the scroll-back here.
    Send-NutshellChord -Session $s -Key PgUp -Shift -SettleMs 300
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
    # difference in the title area). Where the chrome ends is read off the real
    # tab-strip window rather than assumed to be 15% down: that fraction is
    # only right at one window size and one DPI, and the window size is now
    # whatever the work area allowed.
    $top = 0.15
    try { $st = Get-NutshellTabStripRegion -Session $s; $top = [Math]::Min(0.5, $st.Y + $st.H + 0.01) } catch { }
    $region = @{ X = 0.0; Y = $top; W = 0.6; H = (0.98 - $top) }
    $hb = Get-NutshellRegionHash -Path $before -Region $region
    $ha = Get-NutshellRegionHash -Path $after  -Region $region
    # Regression guard for the bug this case found: WM_SIZE had no
    # SIZE_MINIMIZED guard, so the resize-to-iconic re-ran the smart-scroll
    # offset recompute against a transient near-zero row count and never
    # corrected it on restore -- a scrolled-back view came back one page up.
    # Fixed in v1.1.12 (PR #12); asserted normally from here on.
    Assert-True ($hb -eq $ha) "terminal content differs after minimise/restore (see minimise_before/after.png)"
    "minimised (IsIconic true), restored (IsIconic false), scrolled-back view unchanged"
}

# ---- WINDOW-2: fullscreen toggle changes the PTY size ----------------------------
Invoke-Case "fullscreen_toggle_changes_pty" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    # Deliberately narrower than the screen, expressed as a fraction of the
    # work area rather than a flat 900px: fullscreen goes to the whole monitor,
    # so the baseline only has room to grow if it is smaller than the monitor,
    # which "900" is not guaranteed to be on an arbitrary desktop.
    $wa = Get-NutshellWorkArea
    $base = Set-NutshellWindowSize -Session $s -Width ([int]($wa.Width * 0.7)) -Height ([int]($wa.Height * 0.7))
    Send-NutshellLine -Session $s -Line 'echo FS_COLS_0=$(tput cols)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "FS_COLS_0=(\d+)" -TimeoutSec 5) "no baseline size report"
    $cols0 = [int][regex]::Match((Get-NutshellLogText -Session $s), "FS_COLS_0=(\d+)").Groups[1].Value

    [NutshellNative]::PostMessage($s.Main, $WM_COMMAND, [IntPtr]$IDM_VIEW_FULLSCREEN, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 900
    Send-NutshellLine -Session $s -Line 'echo FS_COLS_1=$(tput cols)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "FS_COLS_1=(\d+)" -TimeoutSec 5) "no size report after entering fullscreen"
    $cols1 = [int][regex]::Match((Get-NutshellLogText -Session $s), "FS_COLS_1=(\d+)").Groups[1].Value
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "fullscreen_entered.png") | Out-Null
    Assert-True ($cols1 -gt $cols0) "fullscreen did not grow tput cols: $cols0 -> $cols1 (from a $($base.ClientWidth)px-wide client area on a $($wa.Width)px-wide work area; expected the screen to be wider)"

    [NutshellNative]::PostMessage($s.Main, $WM_COMMAND, [IntPtr]$IDM_VIEW_FULLSCREEN, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 900
    Send-NutshellLine -Session $s -Line 'echo FS_COLS_2=$(tput cols)'
    Assert-True (Wait-NutshellLog -Session $s -Pattern "FS_COLS_2=(\d+)" -TimeoutSec 5) "no size report after leaving fullscreen"
    $cols2 = [int][regex]::Match((Get-NutshellLogText -Session $s), "FS_COLS_2=(\d+)").Groups[1].Value
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "fullscreen_exited.png") | Out-Null
    # Back to the *original* width, not merely smaller than fullscreen: the
    # window returns to the same size it had before, so the PTY must report
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
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "close_with_live_session_exits_cleanly")) {
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
# process's memory for the MenuItemData.text field (out of scope for an integration
# helper). What message APIs DO expose regardless of owner-draw -- and what
# this case asserts -- is structure: top-level menu count, each submenu's
# item count (separators included), and each item's real WM_COMMAND id in
# order (GetMenuItemID returns 0 for a separator, the id itself otherwise, and
# -1 for a popup item such as the Edit menu's "Send Key" submenu -- the
# NutshellNative Add-Type block above declares GetMenuItemID's return as a
# plain `int`, so that comes back as -1 directly rather than the unsigned
# 0xFFFFFFFF the Win32 docs describe), hand-derived here from
# create_app_menu()'s call order and cross-checked against src/ui/resource.h's
# IDM_* constants. The Edit menu's "Send Key" popup (special-keys design
# section 4A) is walked as its own submenu (GetSubMenu(editMenu, 3)).
# Run twice. With the AI assistant off (the default config: ai_enabled false)
# the menus have none of its items: File has no "Save AI Chat..." (2007) and no
# separator of its own, View is Fullscreen (2021) alone. The twin
# (menus_open_and_list_items_ai_on, ai_enabled true) keeps the full lists.
foreach ($aiOn in @($false, $true)) {
$baseName = "menus_open_and_list_items"
$caseName = if ($aiOn) { $baseName + "_ai_on" } else { $baseName }
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains $caseName)) {
    $name = $caseName
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath -Settings @{ ai_enabled = $aiOn }
    $session = $null
    $ok = $false; $detail = ""
    try {
        $session = Start-Nutshell -Env $testEnv -ExtraArgs @("-nc")
        Start-Sleep -Milliseconds 500
        $hMenu = [NutshellNative]::GetMenu($session.Main)
        Assert-True ($hMenu -ne [IntPtr]::Zero) "main window has no menu"
        $topCount = [NutshellNative]::GetMenuItemCount($hMenu)
        Assert-True ($topCount -eq 4) "expected 4 top-level menus (File/Edit/View/Help), got $topCount"

        # 0 = separator; -1 = a popup item (submenu, walked separately below
        # for Edit's "Send Key"); other values are the IDM_* constants from
        # resource.h. New Edit layout: Copy, Paste, Select All, "Send Key"
        # (popup), separator, Settings.
        $expected = @{
            0 = @(2001, 2008, 0, 2002, 2003, 0, 2004, 2005, 0, 2007, 0, 2006)  # File: new session, close tab, ...
            1 = @(2010, 2011, 2012, -1, 0, 2013)                          # Edit
            2 = @(2020, 2022, 2021)                                       # View
            3 = @(2033, 2029, 0, 2031, 2032, 0, 2030)                     # Help: shortcuts, guide, mark issue, log folder, about
        }
        if (-not $aiOn) {
            # Switch off: no Save AI Chat... and its separator, no AI Assist / Undock AI Assist.
            $expected[0] = @(2001, 2008, 0, 2002, 2003, 0, 2004, 2005, 0, 2006)
            $expected[2] = @(2021)
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

        # The Edit menu's "Send Key" submenu (item index 3): F1..F12, sep,
        # Page Up, Page Down, sep, Ctrl+V, Shift+Insert, Ctrl+=, Ctrl+-, sep,
        # Send Next Key Raw, Send Next Key with Alt.
        $editMenu = [NutshellNative]::GetSubMenu($hMenu, 1)
        $sendKeyMenu = [NutshellNative]::GetSubMenu($editMenu, 3)
        Assert-True ($sendKeyMenu -ne [IntPtr]::Zero) "Edit menu item 3 (Send Key) has no submenu"
        $wantSendKey = @(2040, 2041, 2042, 2043, 2044, 2045, 2046, 2047, 2048, 2049, 2050, 2051, `
                          0, 2052, 2053, 0, 2054, 2055, 2056, 2057, 0, 2058, 2059)
        $countSK = [NutshellNative]::GetMenuItemCount($sendKeyMenu)
        Assert-True ($countSK -eq $wantSendKey.Count) "Send Key submenu has $countSK item(s), expected $($wantSendKey.Count)"
        for ($k = 0; $k -lt $countSK; $k++) {
            $idSK = [NutshellNative]::GetMenuItemID($sendKeyMenu, $k)
            Assert-True ($idSK -eq $wantSendKey[$k]) "Send Key item $k : id $idSK, expected $($wantSendKey[$k])"
        }

        $path = Join-Path $Artifacts "$name.png"
        Save-NutshellScreenshot -Session $session -Path $path | Out-Null
        $fileN = $expected[0].Count; $viewN = $expected[2].Count
        $detail = "AI assistant " + $(if ($aiOn) { "on" } else { "off" }) + ": 4 top-level menus; File $fileN items, Edit 6 (incl. Send Key popup), View $viewN, Help 7; Send Key submenu 23 items (F1-F12, PgUp/PgDn, Ctrl+V/Shift+Insert/Ctrl+=/Ctrl+-, raw/Alt one-shots); every WM_COMMAND id matched resource.h/create_app_menu() in order (captions not API-observable -- owner-drawn, see case comment)"
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
}

# ---- HELP-1: Help > Keyboard Shortcuts (cheat-sheet spec section 3.2) ----------
# The sheet is custom-painted (class Nutshell_Shortcuts, title "Keyboard
# Shortcuts"), so it is captured by its own handle and keys are posted to that
# handle. -nc: no SSH host needed. 2033 is IDM_HELP_SHORTCUTS (resource.h).
$IDM_HELP_SHORTCUTS = 2033
$WM_KEYDOWN_SHEET   = 0x0100
$VK_ESCAPE_SHEET    = 0x1B

function Open-NutshellShortcutsSheet {
    <# Post Help > Keyboard Shortcuts to the main window and return the sheet's
       HWND; throws if no sheet appears. #>
    param([Parameter(Mandatory)] $Session)
    [NutshellNative]::PostMessage($Session.Main, $WM_COMMAND, [IntPtr]$IDM_HELP_SHORTCUTS, [IntPtr]::Zero) | Out-Null
    $dlg = Wait-NutshellDialog -Session $Session -Title "^Keyboard Shortcuts$" -TimeoutSec 10
    Assert-True ($dlg -ne [IntPtr]::Zero) "Help > Keyboard Shortcuts opened no Nutshell_Shortcuts window"
    Start-Sleep -Milliseconds 400   # let the first paint land before a capture
    return $dlg
}

function Get-NutshellShortcutsSheets {
    param([Parameter(Mandatory)] $Session)
    return @(Get-NutshellWindows -Session $Session | Where-Object { ($_ -split "`t")[1] -eq "Nutshell_Shortcuts" })
}

Invoke-Case -Name "help_shortcuts_sheet_opens_and_closes" -Settings @{} -ExtraArgs @("-nc") -Body {
    param($s)
    $dlg = Open-NutshellShortcutsSheet -Session $s
    $path = Join-Path $Artifacts "help_shortcuts_sheet.png"
    Save-NutshellScreenshot -Session $s -Path $path -Hwnd ([long]$dlg) | Out-Null
    Assert-True (Test-NutshellCaptureNonBlank -Path $path) "the sheet's capture looks blank: $path"
    Assert-True (-not [NutshellNative]::IsWindowEnabled($s.Main)) "the main window stayed enabled under the modal sheet"

    # A second Help > Keyboard Shortcuts while it is open opens nothing more.
    [NutshellNative]::PostMessage($s.Main, $WM_COMMAND, [IntPtr]$IDM_HELP_SHORTCUTS, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 800
    $n = (Get-NutshellShortcutsSheets -Session $s).Count
    Assert-True ($n -eq 1) "a second Help > Keyboard Shortcuts left $n sheets open, expected 1"

    # Esc posted to the sheet's own handle closes it.
    [NutshellNative]::PostMessage($dlg, $WM_KEYDOWN_SHEET, [IntPtr]$VK_ESCAPE_SHEET, [IntPtr]::Zero) | Out-Null
    $deadline = (Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $deadline -and [NutshellNative]::IsWindow($dlg)) { Start-Sleep -Milliseconds 100 }
    Assert-True (-not [NutshellNative]::IsWindow($dlg)) "Esc posted to the sheet did not close it"
    Assert-True ([NutshellNative]::IsWindowEnabled($s.Main)) "the main window is still disabled after the sheet closed"
    "opened by WM_COMMAND 2033, captured by handle (non-blank), main window disabled while open; a second open ignored; Esc closed it; main window enabled again"
}

Invoke-Case -Name "help_shortcuts_sheet_fits_the_screen" -Settings @{} -ExtraArgs @("-nc") -Body {
    param($s)
    $dlg = Open-NutshellShortcutsSheet -Session $s
    $r = New-Object NutshellNative+RECT
    [NutshellNative]::GetWindowRect($dlg, [ref]$r) | Out-Null
    $wa = Get-NutshellWorkArea
    $dpi = [int][NutshellNative]::GetDpiForWindow($dlg)
    $inside = ($r.L -ge $wa.X) -and ($r.T -ge $wa.Y) -and ($r.R -le ($wa.X + $wa.Width)) -and ($r.B -le ($wa.Y + $wa.Height))
    Assert-True $inside ("sheet {0},{1}-{2},{3} leaves the work area {4},{5} {6}x{7} at {8} DPI" -f $r.L, $r.T, $r.R, $r.B, $wa.X, $wa.Y, $wa.Width, $wa.Height, $dpi)
    # CSB_CLASS_NAME (custom_scrollbar.h); the sheet creates one only when its
    # page is taller than its view.
    $bars = @([NutshellNative]::ListChildren($dlg) | Where-Object { ($_ -split "`t")[2] -eq "Nutshell_CustomScrollbar" })
    # Maintainer's ruling: at 96 DPI on a 1920x1040 (or larger) work area the
    # whole sheet fits on one page. Other DPIs and work areas may scroll.
    if ($dpi -eq 96 -and $wa.Width -ge 1920 -and $wa.Height -ge 1040) {
        Assert-True ($bars.Count -eq 0) ("the sheet scrolls at 96 DPI on a {0}x{1} work area; it must fit on one page there" -f $wa.Width, $wa.Height)
    }
    $how = if ($bars.Count -gt 0) { "scrolls" } else { "one page" }
    [NutshellNative]::PostMessage($dlg, $WM_KEYDOWN_SHEET, [IntPtr]$VK_ESCAPE_SHEET, [IntPtr]::Zero) | Out-Null
    "sheet {0}x{1} inside the {2}x{3} work area at {4} DPI; {5}" -f ($r.R - $r.L), ($r.B - $r.T), $wa.Width, $wa.Height, $dpi, $how
}

function Get-NutshellSettingsWindows {
    param([Parameter(Mandatory)] $Session)
    return @(Get-NutshellWindows -Session $Session | Where-Object { ($_ -split "`t")[1] -eq "Nutshell_Settings" })
}

# The Settings dialog's help tips (settings_help.c): one tooltips_class32
# window holds a tool per labelled control, and the API key eye button keeps
# its own tip. A tip window is hidden until hovered, and Get-NutshellWindows
# lists visible windows only, so this walks every top-level window itself.
# A mismatch in the counts means a control gained or lost a tool. The case
# also posts Edit > Settings a second time: the re-entry guard must leave
# exactly one Settings window, the same one.
Invoke-Case -Name "settings_help_tool_count" -Settings @{} -ExtraArgs @("-nc") -Body {
    param($s)
    $dlg = Open-NutshellSettings -Session $s
    $counts = New-Object System.Collections.ArrayList
    $after = [IntPtr]::Zero
    for ($i = 0; $i -lt 500; $i++) {
        $h = [NutshellNative]::FindWindowEx([IntPtr]::Zero, $after, "tooltips_class32", [IntPtr]::Zero)
        if ($h -eq [IntPtr]::Zero) { break }
        $after = $h
        [uint32]$ownerPid = 0
        [NutshellNative]::GetWindowThreadProcessId($h, [ref]$ownerPid) | Out-Null
        if ($ownerPid -ne [uint32]$s.Process.Id) { continue }
        if ([NutshellNative]::GetParent($h) -ne $dlg) { continue }
        # TTM_GETTOOLCOUNT (WM_USER + 13)
        $n = [NutshellNative]::SendMsg($h, 0x040D, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
        [void]$counts.Add($n)
    }
    $seen = (@($counts | Sort-Object) -join ", ")
    # 84 before the Voice page. The Voice page adds 23 tools to the dialog's
    # window: 3 tickboxes (Enable voice, Use current AI assistant, Review
    # before sending) = 3; 9 labelled rows (Provider, API key, Base URL, Model,
    # Language, Input device, Push-to-talk key, Mode, Mic start delay) each with
    # a label and a control = 18; the Test button = 1; the editable Model
    # combo's edit gets a second tool = 1. 84 + 23 = 107, plus 3 for the Live model row (label, combo, the combo edit's second tool; the dim note has no tip) = 110. The new API key box
    # keeps an eye tip of its own: one more tooltip window holding 1 tool, so
    # the windows hold [1, 1, 110]. Not run here (no pwsh); check on Windows.
    Assert-True ($seen -eq "1, 1, 110") "the Settings dialog's tooltip windows hold tools [$seen], expected [1, 1, 110]"

    # A second Edit > Settings while the dialog is open opens nothing more.
    [NutshellNative]::PostMessage($s.Main, $WM_COMMAND, [IntPtr]$IDM_EDIT_SETTINGS, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 800
    $open = (Get-NutshellSettingsWindows -Session $s).Count
    Assert-True ($open -eq 1) "a second Edit > Settings left $open Settings windows open, expected 1"
    Assert-True ([NutshellNative]::IsWindow($dlg)) "the first Settings window is gone after a second Edit > Settings"

    Close-NutshellDialog -Dialog $dlg -Button Cancel
    "tooltip windows of the Settings dialog hold [$seen] tools (110 labelled controls incl. the Enable AI assistant box and the Voice page, 2 API key eyes of 1 each); a second Edit > Settings left one Settings window"
}

# The sheet on all four themes, for the eye: --ui-demo=shortcuts opens it over
# the demo window, so no host and no config theme edit is needed. Captured by
# handle into artifacts\gallery\<Theme>-shortcuts.png beside ui_gallery's.
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "help_shortcuts_sheet_gallery")) {
    $name = "help_shortcuts_sheet_gallery"
    Write-Host ("[RUN ] " + $name)
    $galleryDir = Join-Path $Artifacts "gallery"
    New-Item -ItemType Directory -Force $galleryDir | Out-Null
    $themes = @("Onyx Synapse", "Onyx Light", "Sage & Sand", "Moss & Mist")
    $ok = $true
    $notes = New-Object System.Collections.ArrayList
    foreach ($theme in $themes) {
        $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
        $session = $null
        try {
            $session = Start-Nutshell -Env $testEnv -ExtraArgs @("--ui-demo=shortcuts", "--theme", $theme)
            $dlg = Wait-NutshellDialog -Session $session -Title "^Keyboard Shortcuts$" -TimeoutSec 10
            Assert-True ($dlg -ne [IntPtr]::Zero) "--ui-demo=shortcuts opened no sheet"
            Start-Sleep -Milliseconds 400
            $path = Join-Path $galleryDir ("{0}-shortcuts.png" -f (ConvertTo-NutshellFileToken $theme))
            Save-NutshellScreenshot -Session $session -Path $path -Hwnd ([long]$dlg) | Out-Null
            Assert-True (Test-NutshellCaptureNonBlank -Path $path) "capture looks blank: $path"
            [void]$notes.Add("${theme}: ok")
        } catch {
            $ok = $false
            [void]$notes.Add("${theme}: " + $_.Exception.Message)
        } finally {
            if ($session) { Stop-Nutshell -Session $session }
            for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
                Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
                if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
            }
        }
    }
    $detail = "sheet captured by handle on 4 themes into artifacts\gallery\<Theme>-shortcuts.png -- " + ($notes -join "; ")
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}

# The notice dialog on all four themes, for the eye: --ui-demo=notice-info (the
# upgrade sample) and --ui-demo=notice-warning (the invalid-config sample) open
# it over the demo window, so no host and no config theme edit is needed.
# Captured by handle into artifacts\gallery\<Theme>-notice-info.png and
# -notice-warning.png beside ui_gallery's.
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "notice_dialog_gallery")) {
    $name = "notice_dialog_gallery"
    Write-Host ("[RUN ] " + $name)
    $galleryDir = Join-Path $Artifacts "gallery"
    New-Item -ItemType Directory -Force $galleryDir | Out-Null
    $themes = @("Onyx Synapse", "Onyx Light", "Sage & Sand", "Moss & Mist")
    $ok = $true
    $notes = New-Object System.Collections.ArrayList
    foreach ($theme in $themes) {
        foreach ($variant in @(
                @{ State = "notice-info";    Title = "^Welcome to Nutshell " },
                @{ State = "notice-warning"; Title = "^Your configuration couldn.t be loaded$" })) {
            $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
            $session = $null
            try {
                $session = Start-Nutshell -Env $testEnv -ExtraArgs @(("--ui-demo=" + $variant.State), "--theme", $theme)
                $dlg = Wait-NutshellDialog -Session $session -Title $variant.Title -TimeoutSec 10
                Assert-True ($dlg -ne [IntPtr]::Zero) ("--ui-demo=" + $variant.State + " opened no notice")
                Start-Sleep -Milliseconds 400
                $path = Join-Path $galleryDir ("{0}-{1}.png" -f (ConvertTo-NutshellFileToken $theme), $variant.State)
                Save-NutshellScreenshot -Session $session -Path $path -Hwnd ([long]$dlg) | Out-Null
                Assert-True (Test-NutshellCaptureNonBlank -Path $path) "capture looks blank: $path"
                [void]$notes.Add("${theme} " + $variant.State + ": ok")
            } catch {
                $ok = $false
                [void]$notes.Add("${theme} " + $variant.State + ": " + $_.Exception.Message)
            } finally {
                if ($session) { Stop-Nutshell -Session $session }
                for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
                    Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
                    if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
                }
            }
        }
    }
    $detail = "notice captured by handle on 4 themes x 2 tones into artifacts\gallery\<Theme>-notice-info.png and -notice-warning.png -- " + ($notes -join "; ")
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}

# Settings > Voice on all four themes, for the eye: --ui-demo=settings-voice
# opens Settings on the Voice page with the AI assistant and Enable voice
# ticked in memory only (never saved), and no microphone list: the demo loads
# no audio. Captured by handle into artifacts\gallery\<Theme>-settings-voice.png.
# Unrun here (no pwsh on automaton); run it on the Windows box.
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "settings_voice_gallery")) {
    $name = "settings_voice_gallery"
    Write-Host ("[RUN ] " + $name)
    $galleryDir = Join-Path $Artifacts "gallery"
    New-Item -ItemType Directory -Force $galleryDir | Out-Null
    $themes = @("Onyx Synapse", "Onyx Light", "Sage & Sand", "Moss & Mist")
    $ok = $true
    $notes = New-Object System.Collections.ArrayList
    foreach ($theme in $themes) {
        $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
        $session = $null
        try {
            $session = Start-Nutshell -Env $testEnv -ExtraArgs @("--ui-demo=settings-voice", "--theme", $theme)
            $dlg = Wait-NutshellDialog -Session $session -Title "^Settings$" -TimeoutSec 10
            Assert-True ($dlg -ne [IntPtr]::Zero) "--ui-demo=settings-voice opened no Settings window"
            Start-Sleep -Milliseconds 600
            $path = Join-Path $galleryDir ("{0}-settings-voice.png" -f (ConvertTo-NutshellFileToken $theme))
            Save-NutshellScreenshot -Session $session -Path $path -Hwnd ([long]$dlg) | Out-Null
            Assert-True (Test-NutshellCaptureNonBlank -Path $path) "capture looks blank: $path"
            [void]$notes.Add("${theme}: ok")
        } catch {
            $ok = $false
            [void]$notes.Add("${theme}: " + $_.Exception.Message)
        } finally {
            if ($session) { Stop-Nutshell -Session $session }
            for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
                Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
                if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
            }
        }
    }
    $detail = "Settings > Voice captured by handle on 4 themes into artifacts\gallery\<Theme>-settings-voice.png -- " + ($notes -join "; ")
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}

# Voice off, the assistant on, the panel open: neither Right Shift (the
# default push-to-talk key) nor Right Ctrl (an option) does anything of its
# own. A posted Right Shift down/up (VK_SHIFT, scan code 0x36, extended bit
# clear) and a posted Right Ctrl down/up (VK_CONTROL with the extended-key
# bit), both through Send-NutshellChord's posting path, sent to the AI input
# leave no mark, and the posted 'a' that follows reads back as 'a' there: no
# push-to-talk filter swallowed or delayed anything. A posted key-down does not
# change the keyboard state, so the Shift here does not capitalise the 'a'; the
# case asserts what is true, that the input reads exactly 'a' and the panel has
# no mic button (so no recording cue could have appeared). Unrun here (no
# pwsh); run it on the Windows box.
Invoke-Case -Name "voice_off_right_shift_reaches_input" -Settings @{ ai_enabled = $true; ai_api_key = "" } -ExtraArgs @("-nc") -Body {
    param($s)
    $p = Open-NutshellAiPanel -Session $s
    $inputBox = Get-NutshellControl -Dialog $p -Id 4002                                  # IDC_CHAT_INPUT
    Assert-True ($inputBox -ne [IntPtr]::Zero) "the AI panel has no input box"
    Assert-True ([NutshellNative]::GetDlgItem($p, 4016) -eq [IntPtr]::Zero) "the AI panel has a mic button with voice off"   # IDC_CHAT_MIC
    Send-NutshellChord -Session $s -Key RightShift -Hwnd $inputBox -SettleMs 300
    Send-NutshellChord -Session $s -Key RightCtrl -Hwnd $inputBox -SettleMs 300
    [NutshellNative]::PostMessage($inputBox, $script:WM_CHAR, [IntPtr][int][char]'a', [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 400
    $text = Get-NutshellControlText -Control $inputBox
    Assert-True ($text -eq "a") "the AI input reads '$text' after Right Shift, Right Ctrl and 'a', expected 'a'"
    Assert-True ([NutshellNative]::GetDlgItem($p, 4016) -eq [IntPtr]::Zero) "a mic button appeared after Right Shift with voice off"
    "voice off: a posted Right Shift and Right Ctrl down/up and then 'a' left exactly 'a' in the AI input; no mic button"
}
