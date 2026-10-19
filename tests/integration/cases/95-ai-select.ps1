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

# Every point a drag is aimed at must lie inside the list's client area, or the
# drag auto-scrolls on each move past the edge and the focus can never land on
# the word the query named. The geometry (the list's height in physical pixels
# against the reply's line positions) depends on the DPI and the work area, so
# a case that cannot place its points says so instead of dragging and
# reporting a clipboard that was never a fair test.
function Assert-AiSelRectVisible {
    param($List, $Rect, [string] $Word)
    $c = [NutshellNative]::ClientSize($List)
    if ($Rect.Y + $Rect.H -gt $c.B) {
        throw "inconclusive at this DPI/work area: list $($c.B) px tall, '$Word' at y=$($Rect.Y)"
    }
    if ($Rect.X + $Rect.W -gt $c.R) {
        throw "inconclusive at this DPI/work area: list $($c.R) px wide, '$Word' at x=$($Rect.X)"
    }
}

# One --ui-demo case: start, size the window, find the list, scroll it to the
# top, run the body (param $s, $list; returns the detail string), record the
# result. The exe is stopped and its scratch directory removed on every path.
# The window is sized in physical pixels from the 96-DPI numbers (1400 wide,
# $Height tall) times the window's DPI / 96, because the list's height decides
# which words are painted and where; Set-NutshellWindowSize clamps to the work
# area. The detail string carries the DPI and the achieved list size.
function Invoke-AiSelectCase {
    param([string] $Name, [int] $Height = 900, [scriptblock] $Body)
    if (-not (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains $Name))) { return }
    Write-Host ("[RUN ] " + $Name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
    $session = $null
    $ok = $false; $detail = ""; $geometry = ""
    try {
        $session = Start-Nutshell -Env $testEnv -ExtraArgs @("--ui-demo=chat")
        $dpi = Get-NutshellWindowDpi -Session $session
        $sz = Set-NutshellWindowSize -Session $session `
            -Width ([int](1400 * $dpi / 96)) -Height ([int]($Height * $dpi / 96))
        Start-Sleep -Seconds 2
        $list = Get-NutshellChatList -Session $session
        if ($list -eq [IntPtr]::Zero) { throw "AI panel message list not found in --ui-demo=chat" }
        $lc = [NutshellNative]::ClientSize($list)
        $geometry = "dpi $dpi, window client $($sz.ClientWidth)x$($sz.ClientHeight), list $($lc.R - $lc.L)x$($lc.B - $lc.T)"
        # The chat demo already opens at the top (ui_demo_scrolls_to_top); this
        # scroll is a safeguard, because a word above the visible area is not
        # answered.
        [NutshellNative]::SendMsg($list, $script:WM_VSCROLL_AI, [IntPtr]6, [IntPtr]::Zero) | Out-Null
        Start-Sleep -Milliseconds 500
        $out = @(& $Body $session $list)
        $detail = [string]$out[-1]
        if ($geometry) { $detail += " ($geometry)" }
        $ok = $true
    } catch {
        $detail = $_.Exception.Message
        if ($geometry) { $detail += " ($geometry)" }
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
    $list = Get-NutshellChatList -Session $s
    Assert-AiSelRectVisible $list $a "mounts"
    Assert-AiSelRectVisible $list $b "first:"
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
    Assert-AiSelRectVisible $list $a "disk"
    Assert-AiSelRectVisible $list $b "found"
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
    Assert-AiSelRectVisible $list $a "disk"
    # The reply's last line must be out of sight when the drag starts. A $null
    # answer is fine: the list answers only for items a paint touched, so a
    # reply that starts below the painted range is "below" too, and the drag
    # has to scroll to reach it. A line that is painted must start below the
    # list's bottom edge.
    if ($null -ne $w) {
        Assert-True ($w.Y -ge $c.B) `
            "the reply's last line is already visible (y=$($w.Y) < $($c.B)); the window is too tall for this case"
        $pre = "precondition: the reply's last line is painted below the list (y=$($w.Y) >= $($c.B))"
    } else {
        $pre = "precondition: the reply's last line is not painted yet, so it is below the list"
    }
    Set-Clipboard -Value "SENTINEL_SELECT_3"
    Send-NutshellChatDrag -Session $s -FromX ($a.X + 1) -FromY ($a.Y + [int]($a.H / 2)) `
        -ToX ([int]($c.R / 2)) -ToY ($c.B + 40) -Steps 4 -HoldMoves 80
    Send-NutshellChord -Session $s -Key C -Ctrl -Hwnd $list
    $got = Get-AiSelClipboard
    Assert-True ($got.StartsWith("disk usage on this box")) "clipboard does not start at the anchor: '$got'"
    Assert-True ($got.EndsWith("Want me to rotate and compress the old logs?")) `
        "the drag past the bottom edge did not reach the end of the reply: '$got'"
    # Scrolled: "disk" (item 0, at $a.Y before the drag) is now higher up, or
    # has left the painted range altogether. This does not depend on whether
    # the reply's last line was painted before the drag.
    $a2 = Get-NutshellChatWordRect -Session $s -Word "disk" -Item 0
    Assert-True ($null -eq $a2 -or $a2.Y -lt $a.Y) `
        "the list did not scroll ('disk' was at y=$($a.Y), is at y=$(if ($null -ne $a2) { $a2.Y } else { 'not painted' }))"
    "dragging past the bottom edge auto-scrolled and the selection reached the reply's last line ($pre)"
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
