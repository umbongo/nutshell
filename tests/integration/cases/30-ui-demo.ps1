# 30-ui-demo.ps1 -- cases driven by --ui-demo (no SSH host or AI key needed).
# Dot-sourced by Run-Integration.ps1; depends on its $ActiveTiers/$Only/
# $Artifacts/$results/$WM_COMMAND/$HostName/$User/$KeyPath/$Exe, and its
# Test-NutshellCaptureNonBlank/ConvertTo-NutshellFileToken helpers (defined in
# the driver, not here, since cases\20-ai.ps1's ai_panel_opens_without_key --
# sourced before this file -- needs Test-NutshellCaptureNonBlank too).

# ---- UI gallery ----------------------------------------------------------------
# Contact sheet of every --ui-demo state x theme (Design-System Foundation,
# spec section 5; AI Assist Panel plan task 5 added "nokey"/"nosession").
# Needs no SSH host or key -- --ui-demo never connects -- so this runs as its
# own block, gated on $Only the same way Invoke-Case is, rather than through
# it: it must not call Wait-NutshellShell or Start-NutshellLogging, both of
# which assume a live shell prompt.
#
# $galleryStates is a hand-kept copy of src/core/ui_demo.c's STATE_NAMES,
# minus "local" (tab-chrome-only, covered by its own case,
# 80-local-shell.ps1) -- there is no cheap way for a PowerShell script to
# query the C array at build time, and --ui-demo=<unknown>'s error text
# ("Unknown demo state: <name>") does not enumerate the valid ones. Native
# coverage that this list can't silently drift from ui_demo_states() lives
# in tests/test_ui_demo.c (test_ui_demo_states_list_ends_in_all et al.) --
# keep both lists in sync by hand when a state is added/removed.
# "batches" (docs/superpowers/specs/2026-09-09-pending-command-batches.md)
# is the one state whose whole point is two independent pending cards at
# once -- its capture is the visual proof that both render side by side.
# "thinking" (2026-09-24) is "chat" with a much longer reasoning block, so
# the capture shows the Thinking box at its 50-line cap with a scrollbar.
# "ansi" (2026-10-19) is the 16-colour palette as swatches, so the contact
# sheet shows what each theme's contrast floor does to it (raw on the dark
# themes, floored to 4.5:1 on the light ones).

if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "ui_gallery")) {
    Write-Host ("[RUN ] ui_gallery")
    $galleryDir = Join-Path $Artifacts "gallery"
    New-Item -ItemType Directory -Force $galleryDir | Out-Null
    $galleryStates = @("chat", "approval", "executing", "tool", "error", "empty", "nokey", "nosession", "batches", "ansi", "thinking", "cmddetail", "settings-debug", "policy-auto", "policy-compact", "all")
    $galleryThemes = @("Onyx Synapse", "Onyx Light", "Sage & Sand", "Moss & Mist")
    $galleryOk = $true
    $galleryDetail = New-Object System.Collections.ArrayList
    foreach ($theme in $galleryThemes) {
        foreach ($state in $galleryStates) {
            $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
            $session = $null
            try {
                $session = Start-Nutshell -Env $testEnv -ExtraArgs @("--ui-demo=$state", "--theme", $theme)
                # Clamped to the work area by the helper; the gallery only
                # needs a large, consistent window, so the achieved size is
                # irrelevant here and the return value is discarded.
                Set-NutshellWindowSize -Session $session -Width 1400 -Height 900 | Out-Null
                $fileName = "{0}-{1}.png" -f (ConvertTo-NutshellFileToken $theme), $state
                $path = Join-Path $galleryDir $fileName
                Save-NutshellScreenshot -Session $session -Path $path | Out-Null
                if (-not (Test-NutshellCaptureNonBlank -Path $path)) {
                    throw "capture looks blank: $fileName"
                }
            } catch {
                $galleryOk = $false
                [void]$galleryDetail.Add("$theme/${state}: " + $_.Exception.Message)
            } finally {
                if ($session) { Stop-Nutshell -Session $session }
                for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
                    Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
                    if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
                }
            }
        }
    }
    $galleryReport = $galleryDetail -join "; "
    if ($galleryOk) { Write-Host "[PASS] ui_gallery" } else { Write-Host ("[FAIL] ui_gallery -- " + $galleryReport) }
    [void]$results.Add([pscustomobject]@{ Name = "ui_gallery"; Passed = $galleryOk; Detail = $galleryReport })
}

# ---- Approval card: Run N selected settles without crashing --------------------
# Regression for the v1.0.97 crash fix: build_cmd_card_geometry derived its row
# count from the stale lv->cmd_count left over from before the last settle
# instead of the live walk, so WM_PAINT (which never recalc_layouts) could paint
# a command row against a NULL cmd_items[] slot. --ui-demo=approval seeds a
# live container with one PENDING and one BLOCKED command; IDC_CMD_APPROVE_SEL
# ("Run N selected") drives the exact settle-then-repaint path that crashed.
# Needs no SSH host or AI key -- --ui-demo never connects -- so this runs as
# its own block like ui_gallery above, not through Invoke-Case/Invoke-AiCase
# (both assume a live shell prompt).
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "approval_card_run_selected_settles")) {
    $name = "approval_card_run_selected_settles"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
    $session = $null
    $ok = $false; $detail = ""
    try {
        $session = Start-Nutshell -Env $testEnv -ExtraArgs @("--ui-demo=approval")
        Start-Sleep -Seconds 2
        $p = Get-NutshellAiPanel -Session $session
        if ($p -eq [IntPtr]::Zero) { throw "AI Assist panel did not open in --ui-demo=approval" }
        [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]3045, [IntPtr]::Zero) | Out-Null  # IDC_CMD_APPROVE_SEL
        # Poll rather than a single sleep-then-check: right after an access
        # violation the process can sit in WER's crash-handling dialog for a
        # moment with HasExited still False (window present but frozen), so
        # a lone HasExited check after one sleep can read as "alive" even
        # though the app already crashed -- Responding (a live SendMessage
        # ping) flips False as soon as the UI thread stops pumping messages,
        # well before the process actually terminates.
        $crashed = $false
        for ($i = 0; $i -lt 10; $i++) {
            Start-Sleep -Milliseconds 500
            $session.Process.Refresh()
            if ($session.Process.HasExited -or -not $session.Process.Responding) { $crashed = $true; break }
        }
        Assert-True (-not $crashed) "nutshell.exe crashed (or stopped responding) after Run N selected on the approval card"
        $path = Join-Path $Artifacts "$name.png"
        Save-NutshellScreenshot -Session $session -Path $path | Out-Null
        Assert-True (Test-NutshellCaptureNonBlank -Path $path) "capture looks blank: $name.png"
        $detail = "process survived Run N selected on the approval card; capture non-blank"
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

# Keystroke-free like ui_gallery: --ui-demo never connects, so this needs no
# SSH host or AI key and runs as its own block rather than through Invoke-Case
# (which assumes a live shell prompt via Wait-NutshellShell/Start-NutshellLogging).
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "helpers_theme_pixel_matches_token")) {
    $name = "helpers_theme_pixel_matches_token"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
    $session = $null
    $ok = $false; $detail = ""
    try {
        $themeName = "Onyx Light"
        $session = Start-Nutshell -Env $testEnv -ExtraArgs @("--ui-demo=chat", "--theme", $themeName)
        Set-NutshellWindowSize -Session $session -Width 1400 -Height 900 | Out-Null
        $path = Join-Path $Artifacts "$name.png"
        Save-NutshellScreenshot -Session $session -Path $path | Out-Null
        Assert-True (Test-NutshellCaptureNonBlank -Path $path) "capture looks blank: $name.png"

        # Sample proportionally rather than at a fixed pixel, and take the
        # vertical offset from where the tab strip actually ends
        # (Get-NutshellTabStripRegion, which reads the Nutshell_Tabs child
        # window's real rect) plus a tenth of the window: a fixed "35% down"
        # only clears the title bar, menu bar and tab strip at one window size
        # and one DPI, and Set-NutshellWindowSize clamps the request to the
        # work area, so neither is a constant. 15% in from the left keeps it
        # left of the docked AI panel. Onyx Light's terminal_bg equals its
        # bg_primary (both 0xF5F5F7), chosen deliberately so this point
        # validates bg_primary unambiguously regardless of which of the two
        # panels it lands in.
        $strip = $null
        try { $strip = Get-NutshellTabStripRegion -Session $session } catch { }
        $below = if ($strip) { $strip.Y + $strip.H + 0.10 } else { 0.35 }
        $bmp = New-Object System.Drawing.Bitmap $path
        $sx = [int]($bmp.Width * 0.15); $sy = [int]($bmp.Height * [Math]::Min(0.9, $below))
        $bmp.Dispose()

        $bg = Get-NutshellThemeColor -Name $themeName -Token "bg_primary"
        $near = Test-NutshellPixelNear -Path $path -X $sx -Y $sy -Rgb @($bg.R, $bg.G, $bg.B) -Tolerance 12
        $sampled = Get-NutshellPixel -Path $path -X $sx -Y $sy
        Assert-True $near ("pixel ($sx,$sy) = rgb($($sampled.R),$($sampled.G),$($sampled.B)) does not match " +
            "$themeName bg_primary rgb($($bg.R),$($bg.G),$($bg.B)) within tolerance")
        $detail = "pixel ($sx,$sy) matched $themeName bg_primary rgb($($bg.R),$($bg.G),$($bg.B)) within tolerance"
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

# ---- Light-theme ANSI contrast (spec 2026-10-19-light-theme-ansi-contrast-design.md, section 6) ----
# Both cases run --ui-demo=ansi: 17 swatch lines (palette entries 0-15, then
# dim bright white; src/core/ui_demo.c's ANSI_TERM_TEXT), docked at the narrow
# policy-compact width. Each line's sample is the centre of its second block
# cell (column 5): the cell size, the grid and the terminal's origin come from
# the last render.draw event (debugging on, render category at Trace), so
# nothing here assumes a DPI or a window size. The origin is the tab strip's
# top-left (the client origin: the strip sits at client 0,0) plus the x/y
# renderer_draw() was called with (the left margin and the tab height).
# Keystroke-free like ui_gallery: --ui-demo never connects.

$script:AnsiDemoSettings = @{ debug_enabled = $true; debug_level_render = "trace"; debug_log_dir = "debuglogs" }

function Get-AnsiDemoRelLuminance {
    param([int[]] $Rgb)
    $lin = @(foreach ($c in $Rgb) {
        $v = $c / 255.0
        if ($v -le 0.04045) { $v / 12.92 } else { [Math]::Pow(($v + 0.055) / 1.055, 2.4) }
    })
    return 0.2126 * $lin[0] + 0.7152 * $lin[1] + 0.0722 * $lin[2]
}

function Get-AnsiDemoContrast {
    <# WCAG 2.x ratio, theme_contrast()'s formula (src/core/theme.c). #>
    param([int[]] $A, [int[]] $B)
    $la = Get-AnsiDemoRelLuminance -Rgb $A
    $lb = Get-AnsiDemoRelLuminance -Rgb $B
    return ([Math]::Max($la, $lb) + 0.05) / ([Math]::Min($la, $lb) + 0.05)
}

function Get-AnsiDemoLStar {
    <# CIE L*, theme_lstar()'s formula. #>
    param([int[]] $Rgb)
    $y = Get-AnsiDemoRelLuminance -Rgb $Rgb
    $f = if ($y -gt (216.0 / 24389.0)) { [Math]::Pow($y, 1.0 / 3.0) } else { ((24389.0 / 27.0) * $y + 16.0) / 116.0 }
    return 116.0 * $f - 16.0
}

function Test-AnsiDemoNear {
    param([int[]] $Rgb, [int[]] $Want, [int] $Tolerance = 12)
    for ($k = 0; $k -lt 3; $k++) {
        if ([Math]::Abs($Rgb[$k] - $Want[$k]) -gt $Tolerance) { return $false }
    }
    return $true
}

function Get-AnsiDemoGeometry {
    <# The last render.draw event's grid and cell size, and the terminal's
       top-left in a Save-NutshellScreenshot capture (window coordinates). #>
    param([Parameter(Mandatory)] $Session)
    $dir = Join-Path $Session.Env.Root "debuglogs"
    $m = $null
    $deadline = (Get-Date).AddSeconds(8)
    while ((Get-Date) -lt $deadline) {
        $log = Get-ChildItem -Path (Join-Path $dir "nutshell-*-p*.log") -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -notmatch "\.issue\d+\.log$" } |
               Sort-Object LastWriteTime | Select-Object -Last 1
        if ($log) {
            $lines = @(Get-Content $log.FullName | Where-Object { $_ -match ' render\.draw ' })
            if ($lines.Count -gt 0) {
                $m = [regex]::Match($lines[$lines.Count - 1],
                    '\brows=(\d+) .*\bchar_h=(\d+) char_w=(\d+) cols=(\d+) x=(-?\d+) y=(-?\d+)')
                if ($m.Success) { break }
            }
        }
        Start-Sleep -Milliseconds 300
    }
    if (-not $m -or -not $m.Success) { throw "no render.draw event with char_w/cols/x/y in $dir" }
    $tabs = [NutshellNative]::FindWindowEx($Session.Main, [IntPtr]::Zero, "Nutshell_Tabs", [IntPtr]::Zero)
    if ($tabs -eq [IntPtr]::Zero) { throw "tab strip (class Nutshell_Tabs) not found under the main window" }
    $wr = New-Object NutshellNative+RECT
    $tr = New-Object NutshellNative+RECT
    [NutshellNative]::GetWindowRect($Session.Main, [ref]$wr) | Out-Null
    [NutshellNative]::GetWindowRect($tabs, [ref]$tr) | Out-Null
    $g = [pscustomobject]@{
        Rows  = [int]$m.Groups[1].Value
        CharH = [int]$m.Groups[2].Value
        CharW = [int]$m.Groups[3].Value
        Cols  = [int]$m.Groups[4].Value
        Left  = ($tr.L - $wr.L) + [int]$m.Groups[5].Value
        Top   = ($tr.T - $wr.T) + [int]$m.Groups[6].Value
    }
    $g | Add-Member -NotePropertyName Text -NotePropertyValue ("rows={0} cols={1} char={2}x{3} origin=({4},{5})" -f $g.Rows, $g.Cols, $g.CharW, $g.CharH, $g.Left, $g.Top)
    return $g
}

function Get-AnsiDemoSwatch {
    <# The pixel at the centre of line $Line's second block cell (column 5). #>
    param([Parameter(Mandatory)] [string] $Path, [Parameter(Mandatory)] $Geo, [Parameter(Mandatory)] [int] $Line)
    $x = [int]($Geo.Left + 5.5 * $Geo.CharW)
    $y = [int]($Geo.Top + ($Line + 0.5) * $Geo.CharH)
    $p = Get-NutshellPixel -Path $Path -X $x -Y $y
    return [pscustomobject]@{
        Line = $Line; X = $x; Y = $y
        Rgb  = @([int]$p.R, [int]$p.G, [int]$p.B)
        Hex  = ('{0:X2}{1:X2}{2:X2}' -f $p.R, $p.G, $p.B)
    }
}

function Get-AnsiDemoSamples {
    <# Size the window to the work area's full height, capture it, read the
       grid, refuse to sample a grid the 17 swatch lines do not fit, and check
       alignment on lines 0 and 1 (palette black and red, the same on every
       theme: above 4.5 on the light ones, raw on the dark ones). Returns the
       17 samples and the geometry. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Path, [Parameter(Mandatory)] [string] $What)
    Save-NutshellScreenshot -Session $Session -Path $Path | Out-Null
    $geo = Get-AnsiDemoGeometry -Session $Session
    if ($geo.Rows -lt 17 -or $geo.Cols -lt 24) { throw "$What grid too small to sample: $($geo.Text)" }
    $s = @(for ($i = 0; $i -le 16; $i++) { Get-AnsiDemoSwatch -Path $Path -Geo $geo -Line $i })
    Assert-True (Test-AnsiDemoNear -Rgb $s[0].Rgb -Want @(0x00, 0x00, 0x00)) "$What line 0 is $($s[0].Hex), expected 000000: the swatches are not where the grid says ($($geo.Text))"
    Assert-True (Test-AnsiDemoNear -Rgb $s[1].Rgb -Want @(0xCC, 0x33, 0x33)) "$What line 1 is $($s[1].Hex), expected CC3333: the swatches are not where the grid says ($($geo.Text))"
    return [pscustomobject]@{ Samples = $s; Geo = $geo }
}

function Start-AnsiDemo {
    param([Parameter(Mandatory)] $TestEnv, [Parameter(Mandatory)] [string] $Theme)
    $session = Start-Nutshell -Env $TestEnv -ExtraArgs @("--ui-demo=ansi", "--theme", $Theme)
    $wa = Get-NutshellWorkArea
    Set-NutshellWindowSize -Session $session -Width 1400 -Height $wa.Height | Out-Null
    Start-Sleep -Milliseconds 1000   # the repaint after the resize reaches the log (250 ms writer)
    return $session
}

if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "helpers_ansi_contrast_floor")) {
    $name = "helpers_ansi_contrast_floor"
    Write-Host ("[RUN ] " + $name)
    $ok = $false; $detail = ""
    $notes = New-Object System.Collections.ArrayList
    try {
        foreach ($themeName in @("Onyx Light", "Onyx Synapse")) {
            $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath -Settings $script:AnsiDemoSettings
            $session = $null
            try {
                $session = Start-AnsiDemo -TestEnv $testEnv -Theme $themeName
                $path = Join-Path $Artifacts ("{0}-{1}.png" -f $name, (ConvertTo-NutshellFileToken $themeName))
                $r = Get-AnsiDemoSamples -Session $session -Path $path -What $themeName
                $s = $r.Samples
                if ($themeName -eq "Onyx Light") {
                    $bg = Get-NutshellThemeColor -Name $themeName -Token "terminal_bg"
                    $bgRgb = @($bg.R, $bg.G, $bg.B)
                    for ($i = 0; $i -le 15; $i++) {
                        $c = Get-AnsiDemoContrast -A $s[$i].Rgb -B $bgRgb
                        Assert-True ($c -ge 4.4) ("{0} line {1} is {2}, {3:N2}:1 against terminal_bg (want >= 4.4)" -f $themeName, $i, $s[$i].Hex, $c)
                    }
                    Assert-True (Test-AnsiDemoNear -Rgb $s[11].Rgb -Want @(0x72, 0x72, 0x23)) "$themeName line 11 (SGR 93) is $($s[11].Hex), expected 727223"
                    $l15 = Get-AnsiDemoLStar -Rgb $s[15].Rgb
                    $l16 = Get-AnsiDemoLStar -Rgb $s[16].Rgb
                    Assert-True ($l16 -ge $l15 + 10.0) ("{0} line 16 (dim 97) is {1}, L* {2:N1}; line 15 (97) is {3}, L* {4:N1}: dim should be at least 10 L* lighter" -f $themeName, $s[16].Hex, $l16, $s[15].Hex, $l15)
                } else {
                    Assert-True (Test-AnsiDemoNear -Rgb $s[11].Rgb -Want @(0xFF, 0xFF, 0x55)) "$themeName line 11 (SGR 93) is $($s[11].Hex), expected the raw FFFF55"
                    Assert-True (Test-AnsiDemoNear -Rgb $s[16].Rgb -Want @(0xFF, 0xFF, 0xFF)) "$themeName line 16 (dim 97) is $($s[16].Hex), expected FFFFFF (dim ignored on a dark theme)"
                }
                [void]$notes.Add(("{0} ({1}): line 11 {2}, line 15 {3}, line 16 {4}" -f $themeName, $r.Geo.Text, $s[11].Hex, $s[15].Hex, $s[16].Hex))
            } finally {
                if ($session) { Stop-Nutshell -Session $session }
                for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
                    Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
                    if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
                }
            }
        }
        $detail = $notes -join "; "
        $ok = $true
    } catch {
        $detail = $_.Exception.Message
    }
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}

# The path --theme never exercises (spec 5.6): a theme changed in Settings
# while the terminal shows indexed text. Lines 11 and 15 are far from the
# cursor (end of line 16), so without the forced repaint they would keep the
# old theme's pixels.
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "helpers_theme_switch_repaints")) {
    $name = "helpers_theme_switch_repaints"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath -Settings $script:AnsiDemoSettings
    $session = $null
    $ok = $false; $detail = ""
    try {
        $session = Start-AnsiDemo -TestEnv $testEnv -Theme "Onyx Synapse"
        $before = Get-AnsiDemoSamples -Session $session -Path (Join-Path $Artifacts "$name-before.png") -What "Onyx Synapse"
        Assert-True (Test-AnsiDemoNear -Rgb $before.Samples[11].Rgb -Want @(0xFF, 0xFF, 0x55)) "before the switch line 11 is $($before.Samples[11].Hex), expected FFFF55"

        $dlg = Open-NutshellSettings -Session $session -Page "Appearance"
        $combo = Get-NutshellControl -Dialog $dlg -Id 1020   # IDC_SCHEME_COMBO, src/ui/settings.c
        if ($combo -eq [IntPtr]::Zero) { throw "colour scheme combo (1020) not found on the Appearance page" }
        Select-NutshellCombo -Control $combo -Item "Onyx Light"
        Close-NutshellDialog -Dialog $dlg -Button OK          # Settings' IDOK is Save
        Start-Sleep -Milliseconds 1500

        $after = Get-AnsiDemoSamples -Session $session -Path (Join-Path $Artifacts "$name-after.png") -What "after the switch"
        $s = $after.Samples
        Assert-True (Test-AnsiDemoNear -Rgb $s[11].Rgb -Want @(0x72, 0x72, 0x23)) "after the switch line 11 is $($s[11].Hex), expected Onyx Light's 727223 (FFFF55 means the row kept Onyx Synapse's pixels)"
        Assert-True (Test-AnsiDemoNear -Rgb $s[15].Rgb -Want @(0x6C, 0x6C, 0x6C)) "after the switch line 15 is $($s[15].Hex), expected Onyx Light's 6C6C6C (FFFFFF means the row kept Onyx Synapse's pixels)"
        $detail = "Onyx Synapse -> Onyx Light in Settings: line 11 $($s[11].Hex), line 15 $($s[15].Hex) ($($after.Geo.Text))"
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
