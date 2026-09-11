# 70-cli.ps1 -- CLI flags (integration-coverage.md section 8, CLI-1 split into several
# small cases: -v, -l, -?, an unknown flag, -nc, -h).
# Dot-sourced by Run-Integration.ps1; depends on its Invoke-Case/Assert-True/
# $Artifacts/$HostName/$User/$KeyPath/$Exe/$ActiveTiers/$Only/$results.

function Test-NutshellHostConsole {
    <# Does *this* PowerShell host own an ordinary console screen buffer?

       This is a pre-flight, not a nicety. Start-Process -NoNewWindow hangs
       indefinitely -- 10+ minutes, never returning even past a
       WaitForExit(5000) guard, so the guard cannot save the suite -- when it
       is called from a console-less host, which is exactly what the
       GitHub Actions job is (the runner starts powershell.exe with its
       standard streams redirected and no console allocated) and what the
       nested automation shells this harness is often driven from are. See
       Get-NutshellExeVersion's doc comment in NutshellIT.psm1, which
       documents the same hang for its own version probe and is why that
       function reads VERSIONINFO instead of spawning anything.

       ReadConsoleTail opens "CONOUT$" itself and returns "" when there is no
       console to open, which is the decisive test; UserInteractive is checked
       too so a service-hosted session never takes the -NoNewWindow path. #>
    if (-not [Environment]::UserInteractive) { return $false }
    try { return -not [string]::IsNullOrEmpty([NutshellNative]::ReadConsoleTail(1)) } catch { return $false }
}

function Invoke-NutshellCliCase {
    <# Runs nutshell.exe with -CliArgs and lets it exit on its own (no window,
       no session) -- for -v/-l/-?/an unknown flag, all handled by
       src/main.c's cli_output(): AttachConsole(ATTACH_PARENT_PROCESS) then
       write straight to CONOUT$ when a console is available, else a
       MessageBox titled "Nutshell"/"Nutshell Sessions"/"Nutshell — Usage".
       Both routes are exercised by the same case, whichever this host offers:

       - With a console (Test-NutshellHostConsole true -- an ordinary
         interactive PowerShell): launch with -NoNewWindow so the child shares
         this console, then read the text back with
         [NutshellNative]::ReadConsoleTail. Verified against this tree's
         actual behaviour, not assumed: AttachConsole succeeds and the text is
         readable in under a second.
       - Without one (the runner job, nested automation shells): -NoNewWindow
         is NOT used -- it would hang the whole suite, see
         Test-NutshellHostConsole -- so the exe is launched plainly, finds no
         parent console, and puts its output in a MessageBox, which this then
         reads and dismisses. nutshell.exe is a GUI-subsystem binary, so a
         plain Start-Process allocates no console window for it either way.
         Should it exit *without* a MessageBox on such a host, there is
         nothing readable to assert on and the case SKIPS rather than fails.

       The console text is scoped to this run with a sentinel written
       immediately before launch: ReadConsoleTail reads the *shared* harness
       console, whose scrollback already holds every earlier case's output, so
       an unscoped -match could happily match a previous case's text (cli_help
       looking for "version" would match cli_version_prints's own output, for
       one). The sentinel goes through [NutshellNative]::WriteConsoleLine, not
       Write-Host: when this host's output is redirected to a pipe -- which is
       the normal case here -- Write-Host goes down that pipe and never reaches
       the console screen buffer the exe writes to.

       -Assert receives ($exitCode, $consoleTail-or-$null, $dialogText-or-$null)
       and should Assert-True/throw on failure, then return a short detail
       string, same convention as Invoke-Case's -Body. #>
    param([Parameter(Mandatory)] [string] $Name, [Parameter(Mandatory)] [string[]] $CliArgs,
          [Parameter(Mandatory)] [scriptblock] $Assert, [int] $TimeoutSec = 8)
    if ($ActiveTiers -notcontains "gate") { return }
    if ($Only.Count -gt 0 -and $Only -notcontains $Name) { return }
    Write-Host ("[RUN ] " + $Name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath
    $ok = $false; $detail = ""
    $p = $null
    try {
        $hasConsole = Test-NutshellHostConsole
        $sentinel = "NSCLI-" + $Name + "-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
        # Everything the exe writes lands after the sentinel in the console
        # buffer. If the write itself fails there is no usable console after
        # all, whatever the pre-flight thought -- take the MessageBox route.
        if ($hasConsole -and -not [NutshellNative]::WriteConsoleLine($sentinel)) { $hasConsole = $false }
        if ($hasConsole) {
            $p = Start-Process -FilePath $testEnv.Exe -ArgumentList $CliArgs -WorkingDirectory $testEnv.Root -PassThru -NoNewWindow
        } else {
            $p = Start-Process -FilePath $testEnv.Exe -ArgumentList $CliArgs -WorkingDirectory $testEnv.Root -PassThru
        }
        # See NutshellIT.psm1's Start-Nutshell for why: touch .Handle now,
        # while the process is still alive, or ExitCode reads back "" later.
        $null = $p.Handle
        # One wait for both routes: cli_output() either writes to the console
        # and exits, or puts up a modal MessageBox and stays alive until it is
        # dismissed -- so whichever comes first ends the wait.
        $fakeSession = [pscustomobject]@{ Process = $p }
        $exited = $false; $hDlg = [IntPtr]::Zero
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        while ((Get-Date) -lt $deadline) {
            if ($p.WaitForExit(250)) { $exited = $true; break }
            $dlgWin = (Get-NutshellWindows -Session $fakeSession) | Where-Object { $_ -match "`t#32770`t" } | Select-Object -First 1
            if ($dlgWin) { $hDlg = [IntPtr][long]($dlgWin -split "`t")[0]; break }
        }
        if ($hDlg -ne [IntPtr]::Zero) {
            $static = [NutshellNative]::ListChildren($hDlg) | Where-Object { ($_ -split "`t")[2] -eq "Static" } | Select-Object -First 1
            $msgText = if ($static) { ($static -split "`t")[3] } else { "" }
            Close-NutshellDialog -Dialog $hDlg -Button OK
            $p.WaitForExit(3000) | Out-Null
            $detail = & $Assert $p.ExitCode $null $msgText
            $ok = $true
        } elseif ($exited -and $hasConsole) {
            $tail = [NutshellNative]::ReadConsoleTail(60)
            $cut = $tail.LastIndexOf($sentinel)
            if ($cut -ge 0) { $tail = $tail.Substring($cut + $sentinel.Length) }
            else { throw "sentinel '$sentinel' not found in the console buffer -- cannot tell this run's output from an earlier case's" }
            $detail = & $Assert $p.ExitCode $tail $null
            $ok = $true
        } elseif ($exited) {
            # It attached to *some* console we cannot read (so no MessageBox
            # was shown) and there is nothing for us to assert on. Skipped, not
            # failed -- same convention as Invoke-AiCase with no key: nothing
            # is added to $results, so the tier stays green and the reason is
            # printed.
            Write-Host ("[SKIP] " + $Name + " -- exited without a MessageBox and this host has no readable console for its output")
            return
        } else {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            throw ("nutshell.exe neither exited within ${TimeoutSec}s nor showed a MessageBox " +
                   "(host console: $hasConsole) -- neither of cli_output()'s two routes produced anything")
        }
    } catch {
        $detail = $_.Exception.Message
    } finally {
        if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
        for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
            Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
            if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
        }
    }
    if ($ok) { Write-Host ("[PASS] " + $Name) } else { Write-Host ("[FAIL] " + $Name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $Name; Passed = $ok; Detail = $detail })
}

Invoke-NutshellCliCase "cli_version_prints" @("-v") {
    param($exitCode, $tail, $dlgText)
    Assert-True ($exitCode -eq 0) "exit code was $exitCode, expected 0"
    $text = if ($tail) { $tail } else { $dlgText }
    Assert-True ($text -match "Nutshell\s+\d+\.\d+\.\d+") "no version string found in: [$text]"
    "version '$($Matches[0])' via $(if ($tail) { 'console' } else { 'MessageBox' })"
}

Invoke-NutshellCliCase "cli_list_profiles" @("-l") {
    param($exitCode, $tail, $dlgText)
    Assert-True ($exitCode -eq 0) "exit code was $exitCode, expected 0"
    $text = if ($tail) { $tail } else { $dlgText }
    Assert-True ($text -match "\bit\b") "generated profile name 'it' not found in: [$text]"
    Assert-True ($text -match [regex]::Escape($HostName)) "host '$HostName' not found in: [$text]"
    "listed profile 'it' / host '$HostName' via $(if ($tail) { 'console' } else { 'MessageBox' })"
}

Invoke-NutshellCliCase "cli_help" @("-?") {
    param($exitCode, $tail, $dlgText)
    Assert-True ($exitCode -eq 0) "exit code was $exitCode, expected 0"
    $text = if ($tail) { $tail } else { $dlgText }
    Assert-True ($text -match "Usage") "no usage text found in: [$text]"
    Assert-True ($text -match "version") "usage text missing the --version flag description"
    "usage text shown via $(if ($tail) { 'console' } else { 'MessageBox' })"
}

Invoke-NutshellCliCase "cli_unknown_flag_errors" @("--bogus-flag-xyz") {
    param($exitCode, $tail, $dlgText)
    Assert-True ($exitCode -ne 0) "exit code was 0, expected non-zero for an unknown flag"
    $text = if ($tail) { $tail } else { $dlgText }
    Assert-True ($text -match "Unknown option") "no 'Unknown option' error text found in: [$text]"
    "unknown flag rejected (exit $exitCode) via $(if ($tail) { 'console' } else { 'MessageBox' })"
}

# ---- cli_no_connect_opens_idle: -nc starts without auto-connecting --------------
Invoke-Case "cli_no_connect_opens_idle" @{} {
    param($s)
    $dlg = Wait-NutshellDialog -Session $s -TimeoutSec 2
    Assert-True ($dlg -eq [IntPtr]::Zero) "a dialog appeared with -nc (expected none -- no config problem, nothing to auto-connect)"
    $fullWindow = @{ X = 0.0; Y = 0.0; W = 1.0; H = 1.0 }
    # A TAB_CONNECTING dot pulses (ns_draw_pulse, tabs.c) and repaints every
    # animation tick; an unchanging capture over 5s is evidence nothing is
    # mid-connect, without needing Get-NutshellTabCount (not implementable).
    # Startup itself is not instantly still, though: the empty window's first
    # paint can land without the version label at bottom-right and a second
    # paint a moment later adds it -- seen on the runner while a build ran
    # alongside (2026-09-11, PR #26's gate: t0 without the label, t5 with it,
    # nothing else different). So capture only once the window has held still
    # for 1s (bounded at 10s), then insist it stays that way for 5s. A
    # connecting tab never holds still, so it still fails here, just later.
    $p1 = Join-Path $Artifacts "cli_nc_t0.png"
    $p2 = Join-Path $Artifacts "cli_nc_t5.png"
    Save-NutshellScreenshot -Session $s -Path $p1 | Out-Null
    $h1 = Get-NutshellRegionHash -Path $p1 -Region $fullWindow
    $settled = $false
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 1
        Save-NutshellScreenshot -Session $s -Path $p2 | Out-Null
        $h2 = Get-NutshellRegionHash -Path $p2 -Region $fullWindow
        if ($h2 -eq $h1) { $settled = $true; break }
        Copy-Item $p2 $p1 -Force
        $h1 = $h2
    }
    Assert-True $settled "the window never held still for 1s within 10s of starting with -nc -- looks like a connection attempt (a connecting-state tab pulses)"
    Start-Sleep -Seconds 5
    Save-NutshellScreenshot -Session $s -Path $p2 | Out-Null
    Assert-True ((Get-NutshellRegionHash -Path $p2 -Region $fullWindow) -eq $h1) `
        "the window repainted over 5s with -nc -- looks like a connection attempt (a connecting-state tab pulses)"
    "no dialog, and once still the window stayed still for 5s with -nc: no connection attempt"
} -ExtraArgs @("-nc")

# ---- cli_host_flag_connects: -h <host> resolves the profile by host -------------
# config_find_profile_by_host() (src/ui/window.c's WM_STARTUP_CONNECT,
# CLI_CONNECT_HOST) matches on Profile.host, which New-NutshellTestEnv sets to
# -HostName -- the same profile -sn <name> uses, just looked up differently.
Invoke-Case "cli_host_flag_connects" @{} {
    param($s)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    Send-NutshellLine -Session $s -Line "echo HOST_FLAG_CONNECTED"
    Assert-True (Wait-NutshellLog -Session $s -Pattern "HOST_FLAG_CONNECTED" -TimeoutSec 10) "no shell output reached the log via -h $HostName"
    "connected via -h $HostName (resolved to the generated profile by host)"
} -ExtraArgs @("-h", $HostName)
