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
# $galleryStates is a hand-kept copy of src/core/ui_demo.c's STATE_NAMES --
# there is no cheap way for a PowerShell script to query the C array at
# build time, and --ui-demo=<unknown>'s error text ("Unknown demo state:
# <name>") does not enumerate the valid ones. Native coverage that this
# list can't silently drift from ui_demo_states() lives in
# tests/test_ui_demo.c (test_ui_demo_states_lists_ten_ending_in_all et al.)
# -- keep both lists in sync by hand when a state is added/removed.
# "batches" (docs/superpowers/specs/2026-09-09-pending-command-batches.md)
# is the one state whose whole point is two independent pending cards at
# once -- its capture is the visual proof that both render side by side.

if (($ActiveTiers -contains "bvt") -and ($Only.Count -eq 0 -or $Only -contains "ui_gallery")) {
    Write-Host ("[RUN ] ui_gallery")
    $galleryDir = Join-Path $Artifacts "gallery"
    New-Item -ItemType Directory -Force $galleryDir | Out-Null
    $galleryStates = @("chat", "approval", "executing", "tool", "error", "empty", "nokey", "nosession", "batches", "all")
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
if (($ActiveTiers -contains "bvt") -and ($Only.Count -eq 0 -or $Only -contains "approval_card_run_selected_settles")) {
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
if (($ActiveTiers -contains "bvt") -and ($Only.Count -eq 0 -or $Only -contains "helpers_theme_pixel_matches_token")) {
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
