# 40-helpers.ps1 -- harness self-tests: posted input and dialog helpers.
# Prove the posted-message helpers added to NutshellIT.psm1 (no foreground, no
# focus, no unlocked desktop needed) actually drive the real app end to end.
# Dot-sourced by Run-Integration.ps1; depends on its Invoke-Case/Assert-True/
# $Artifacts.

Invoke-Case "helpers_posted_text_reaches_shell" @{} {
    param($s)
    # The point of this case: everything it does -- Start-NutshellLogging
    # (PostMessage WM_COMMAND), Wait-NutshellShell (posted Enter),
    # Send-NutshellLine (posted WM_CHAR per character + posted Enter) -- goes
    # through PostMessage to the main window's queue, not SendKeys, so it must
    # pass with the desktop locked or another app holding the foreground.
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line "echo POSTED_OK"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "POSTED_OK" -TimeoutSec 10) "posted text never reached the shell"
    "posted keystrokes reached the shell with no foreground/focus dependency"
}

Invoke-Case "helpers_session_manager_opens_and_lists_profile" @{} {
    param($s)
    $dlg = Open-NutshellSessionManager -Session $s
    $list = Get-NutshellControl -Dialog $dlg -Id 1000   # IDC_LIST_SESSIONS
    Assert-True ($list -ne [IntPtr]::Zero) "session list control (IDC_LIST_SESSIONS) not found"
    $items = Get-NutshellListItems -Control $list
    Assert-True (($items | Where-Object { $_ -match [regex]::Escape($s.Env.ProfileName) }).Count -gt 0) `
        "generated profile '$($s.Env.ProfileName)' not in the list: $($items -join ', ')"
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "helpers_session_manager.png") -Hwnd ([long]$dlg) | Out-Null
    Close-NutshellDialog -Dialog $dlg -Button Cancel
    $stillOpen = Wait-NutshellDialog -Session $s -Title "Session Manager" -TimeoutSec 2
    Assert-True ($stillOpen -eq [IntPtr]::Zero) "Session Manager still open after Cancel"
    "listed profile '$($s.Env.ProfileName)' among $($items.Count) item(s); Cancel closed the dialog"
}

Invoke-Case "helpers_settings_opens_every_page" @{} {
    param($s)
    # The nine selectable pages, per src/core/settings_layout.c's NAV_TABLE
    # (the other three rows are non-selectable group headers).
    $pages = @("Appearance", "Terminal", "Logging", "SSH", "Startup", "Provider", "Behaviour", "Web Access", "About")
    $dlg = Open-NutshellSettings -Session $s -Page $pages[0]
    foreach ($page in $pages) {
        Select-NutshellSettingsPage -Dialog $dlg -Page $page
        Start-Sleep -Milliseconds 300
        $token = ($page -replace '\s+', '-')
        $path = Join-Path $Artifacts "helpers_settings_page_$token.png"
        Save-NutshellScreenshot -Session $s -Path $path -Hwnd ([long]$dlg) | Out-Null
        Assert-True (Test-NutshellCaptureNonBlank -Path $path) "capture looks blank: $path"
    }
    Close-NutshellDialog -Dialog $dlg -Button Cancel
    $stillOpen = Wait-NutshellDialog -Session $s -Title "Settings" -TimeoutSec 2
    Assert-True ($stillOpen -eq [IntPtr]::Zero) "Settings still open after Cancel"
    "all $($pages.Count) pages selected and captured; Cancel closed the dialog"
}
