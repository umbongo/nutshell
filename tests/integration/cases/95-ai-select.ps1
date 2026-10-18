# 95-ai-select.ps1 -- character selection in the AI Assist panel (spec
# docs/superpowers/specs/2026-10-18-ai-panel-text-selection-design.md
# section 7). Driven by --ui-demo=chat, so no SSH host or AI key is needed;
# the list says where it painted each word through CLV_MSG_QUERY_WORD
# (Get-NutshellChatWordRect), so no coordinate is hardcoded, and the drag is
# sent mouse messages (Send-NutshellChatDrag), so the cases run on a locked
# desktop like every other posted-input case. Dot-sourced by
# Run-Integration.ps1; depends on its $ActiveTiers/$Only/$Artifacts/$results/
# $HostName/$User/$KeyPath/$Exe and Assert-True.
#
# The --ui-demo=chat thread is two items: 0 = the user turn ("Can you check
# disk usage on this box and tell me what's eating space?"), 1 = the reply
# (src/core/ui_demo.c CHAT_ASSISTANT_MSG), its Thinking body expanded.

# The Thinking text of --ui-demo=chat (src/core/ui_demo.c CHAT_THINKING).
$script:DemoThinking = "The user wants a disk usage summary. I should run " +
    "df -h for the mount table and du on /var/log since that's usually the " +
    "biggest growth area on a box like this. I'll summarize with a table " +
    "and flag the largest files, then offer a concrete next step rather " +
    "than just dumping raw output."

$script:WM_VSCROLL_AI = 0x0115   # WM_VSCROLL; wParam SB_TOP (6) scrolls to the top
$script:IDM_EDIT_COPY = 2010     # src/ui/resource.h

function Get-AiSelClipboard {
    <# The clipboard text, never $null, with CR LF folded to LF so a copy that
       PowerShell or the clipboard chain re-terminated still compares equal to
       the LF text the panel puts there. #>
    $t = Get-Clipboard -Raw
    if ($null -eq $t) { return "" }
    return ([string]$t) -replace "`r`n", "`n"
}

# One --ui-demo case: start, size the window, find the list, scroll it to the
# top, run the body (param $s, $list; returns the detail string), record the
# result. The exe is stopped and its scratch directory removed on every path.
function Invoke-AiSelectCase {
    param([string] $Name, [int] $Height = 900, [scriptblock] $Body)
    if (-not (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains $Name))) { return }
    Write-Host ("[RUN ] " + $Name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
    $session = $null
    $ok = $false; $detail = ""
    try {
        $session = Start-Nutshell -Env $testEnv -ExtraArgs @("--ui-demo=chat")
        Set-NutshellWindowSize -Session $session -Width 1400 -Height $Height | Out-Null
        Start-Sleep -Seconds 2
        $list = Get-NutshellChatList -Session $session
        if ($list -eq [IntPtr]::Zero) { throw "AI panel message list not found in --ui-demo=chat" }
        # The chat demo already opens at the top (ui_demo_scrolls_to_top); this
        # scroll is a safeguard, because a word above the visible area is not
        # answered.
        [NutshellNative]::SendMsg($list, $script:WM_VSCROLL_AI, [IntPtr]6, [IntPtr]::Zero) | Out-Null
        Start-Sleep -Milliseconds 500
        $out = @(& $Body $session $list)
        $detail = [string]$out[-1]
        $ok = $true
    } catch {
        $detail = $_.Exception.Message
        if ($session) { try { Save-NutshellScreenshot -Session $session -Path (Join-Path $Artifacts "$Name-FAIL.png") | Out-Null } catch {} }
    } finally {
        if ($session) { Stop-Nutshell -Session $session }
        for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
            Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
            if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
        }
    }
    if ($ok) { Write-Host ("[PASS] " + $Name) } else { Write-Host ("[FAIL] " + $Name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $Name; Passed = $ok; Detail = $detail })
}

# Select "mounts first:" in the reply by dragging from the left edge of
# "mounts" to the last character of "first:".
function Select-DemoMountsFirst {
    param($s)
    $a = Get-NutshellChatWordRect -Session $s -Word "mounts" -Item 1
    $b = Get-NutshellChatWordRect -Session $s -Word "first:" -Item 1
    Assert-True ($null -ne $a -and $null -ne $b) "'mounts first:' is not painted in the reply"
    Send-NutshellChatDrag -Session $s -FromX ($a.X + 1) -FromY ($a.Y + [int]($a.H / 2)) `
        -ToX ($b.X + $b.W - 1) -ToY ($b.Y + [int]($b.H / 2))
}

Invoke-AiSelectCase "ai_panel_select_copy" -Body {
    param($s, $list)
    Set-Clipboard -Value "SENTINEL_SELECT_1"
    Select-DemoMountsFirst $s
    $after = Get-AiSelClipboard
    Assert-True ($after -ceq "SENTINEL_SELECT_1") "the button-up copied ('$after'): nothing may be copied on release"
    Send-NutshellChord -Session $s -Key C -Ctrl -Hwnd $list
    $got = Get-AiSelClipboard
    Assert-True ($got -ceq "mounts first:") "clipboard holds '$got', want 'mounts first:'"
    "drag + Ctrl+C copied exactly 'mounts first:'; the release copied nothing"
}

Invoke-AiSelectCase "ai_panel_select_across_items" -Body {
    param($s, $list)
    $a = Get-NutshellChatWordRect -Session $s -Word "disk" -Item 0
    $b = Get-NutshellChatWordRect -Session $s -Word "found" -Item 1
    Assert-True ($null -ne $a) "'disk' is not painted in the user bubble"
    Assert-True ($null -ne $b) "'found' is not painted in the reply"
    Set-Clipboard -Value "SENTINEL_SELECT_2"
    Send-NutshellChatDrag -Session $s -FromX ($a.X + 1) -FromY ($a.Y + [int]($a.H / 2)) `
        -ToX ($b.X + $b.W - 1) -ToY ($b.Y + [int]($b.H / 2)) -Steps 8
    Send-NutshellChord -Session $s -Key C -Ctrl -Hwnd $list
    # Kept identical to tests/test_ui_demo.c
    # test_ui_demo_chat_drag_across_items_golden.
    $want = "disk usage on this box and tell me what's eating space?`n`n" +
            $script:DemoThinking + "`n`n" +
            "Disk Usage Summary`n`nI checked the mounts first:`ndf -h /`n`n" +
            "Here's what I found"
    $got = Get-AiSelClipboard
    Assert-True ($got -ceq $want) "clipboard holds:`n$got`nwant:`n$want"
    "user bubble -> Thinking -> heading -> [EXEC] command -> reply joined exactly"
}

Invoke-AiSelectCase "ai_panel_select_scrolls" -Height 480 -Body {
    param($s, $list)
    $c = [NutshellNative]::ClientSize($list)
    $a = Get-NutshellChatWordRect -Session $s -Word "disk" -Item 0
    $w = Get-NutshellChatWordRect -Session $s -Word "Want" -Item 1
    Assert-True ($null -ne $a) "'disk' is not painted in the user bubble"
    Assert-True ($null -ne $w -and $w.Y -ge $c.B) `
        "precondition: the reply's last line must start below the visible list (list $($c.B) px tall)"
    Set-Clipboard -Value "SENTINEL_SELECT_3"
    Send-NutshellChatDrag -Session $s -FromX ($a.X + 1) -FromY ($a.Y + [int]($a.H / 2)) `
        -ToX ([int]($c.R / 2)) -ToY ($c.B + 40) -Steps 4 -HoldMoves 80
    Send-NutshellChord -Session $s -Key C -Ctrl -Hwnd $list
    $got = Get-AiSelClipboard
    Assert-True ($got.StartsWith("disk usage on this box")) "clipboard does not start at the anchor: '$got'"
    Assert-True ($got.EndsWith("Want me to rotate and compress the old logs?")) `
        "the drag past the bottom edge did not reach the end of the reply: '$got'"
    $w2 = Get-NutshellChatWordRect -Session $s -Word "Want" -Item 1
    Assert-True ($null -eq $w2 -or $w2.Y -lt $w.Y) "the list did not scroll"
    "dragging past the bottom edge auto-scrolled and the selection reached the reply's last line"
}

Invoke-AiSelectCase "ai_panel_edit_copy_follows_focus" -Body {
    param($s, $list)
    Select-DemoMountsFirst $s       # the button-down also focuses the list
    Set-Clipboard -Value "SENTINEL_SELECT_4A"
    Send-NutshellCommand -Session $s -Id $script:IDM_EDIT_COPY
    $got = Get-AiSelClipboard
    Assert-True ($got -ceq "mounts first:") "Edit > Copy with the panel focused gave '$got'"
    Set-NutshellFocus -Session $s -Hwnd $s.Main                # the terminal
    Set-Clipboard -Value "SENTINEL_SELECT_4B"
    Send-NutshellCommand -Session $s -Id $script:IDM_EDIT_COPY
    $got2 = Get-AiSelClipboard
    Assert-True ($got2 -ceq "SENTINEL_SELECT_4B") "Edit > Copy with the terminal focused copied the panel's selection ('$got2')"
    "Edit > Copy took the panel's selection only while its list had focus"
}
