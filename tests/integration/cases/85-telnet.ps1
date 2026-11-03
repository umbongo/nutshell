# 85-telnet.ps1 -- telnet sessions against a scripted loopback responder.
# Dot-sourced by Run-Integration.ps1; depends on its Invoke-Case/Assert-True,
# $ActiveTiers/$Only/$HostName/$User and on NutshellIT.psm1's helpers.
#
# No real device is involved. Each case starts a System.Net.Sockets.TcpListener
# on 127.0.0.1:0 in its own runspace (Start-TelnetResponder), points an extra
# saved profile of kind "telnet" at that port, and connects by name. The
# responder plays a script of steps and records every byte the app sends, so a
# case can check both what reached the terminal and what went out on the wire.
#
# Where a case reads what: the telnet status lines ("Connected to ...",
# "Connection refused by ...") and SSH's "Password entry cancelled." are written
# to the terminal only, never to the session log, so they are read back from the
# terminal with Edit > Select All and the clipboard (Get-TelnetTerminalText).
# Device output and local echo do reach the session log once logging is on.
#
# PowerShell 5.1: no ??, no ternary, no &&. Bytes are [byte[]], text on the wire
# is Latin-1 (code page 28591) so one char is one byte.

$script:IDM_EDIT_SELECT_ALL_TN = 2012
$script:PW_ID_EDIT_TN          = 101
$script:TelnetCurrent          = $null

function ConvertTo-TelnetBytes {
    <# A string as Latin-1 bytes (\r, \n and friends already expanded by the caller). #>
    param([Parameter(Mandatory)] [string] $Text)
    return ,([byte[]][Text.Encoding]::GetEncoding(28591).GetBytes($Text))
}

function Test-TelnetBytesContain {
    <# $true when the byte sequence -Pattern occurs anywhere in -Bytes. #>
    param([byte[]] $Bytes, [Parameter(Mandatory)] [byte[]] $Pattern)
    if ($null -eq $Bytes -or $Bytes.Length -eq 0) { return $false }
    $enc = [Text.Encoding]::GetEncoding(28591)
    $hay = $enc.GetString($Bytes)
    $needle = $enc.GetString($Pattern)
    return ($hay.IndexOf($needle, [StringComparison]::Ordinal) -ge 0)
}

function Start-TelnetResponder {
    <# Listen on 127.0.0.1:<free port>, accept one client and play -Steps:
         @{ DelayMs = n }                         sleep
         @{ Send = [byte[]] }                     write to the client
         @{ Expect = '<regex>'; TimeoutMs = n }   wait until the bytes received
             since the previous Expect, read as Latin-1, match the regex; a miss
             or a hang-up records the failure in State.Failure and stops.
       After the last step it keeps recording until Stop-TelnetResponder.
       Returns @{ Port; Handle; PS; Listener; State }. #>
    param([Parameter(Mandatory)] [hashtable[]] $Steps)

    $listener = [System.Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port

    $state = [hashtable]::Synchronized(@{
        Abort    = $false
        Failure  = ""
        Received = (New-Object System.IO.MemoryStream)
    })

    $work = {
        param($Listener, $PlayList, $State)
        $latin1 = [Text.Encoding]::GetEncoding(28591)
        $client = $null
        try {
            $acceptBy = [DateTime]::UtcNow.AddSeconds(60)
            while (-not $Listener.Pending()) {
                if ($State.Abort) { return }
                if ([DateTime]::UtcNow -gt $acceptBy) { $State.Failure = "no client connected within 60 s"; return }
                Start-Sleep -Milliseconds 50
            }
            $client = $Listener.AcceptTcpClient()
            $stream = $client.GetStream()
            $buf = New-Object byte[] 4096
            $mark = 0       # offset into Received where the next Expect starts

            # Pull whatever is waiting into Received. Returns $false once the peer has closed.
            $pump = {
                $alive = $true
                while ($stream.DataAvailable) {
                    $n = $stream.Read($buf, 0, $buf.Length)
                    if ($n -le 0) { $alive = $false; break }
                    [void]$State.Received.Write($buf, 0, $n)
                }
                if ($alive -and $client.Client.Poll(0, [Net.Sockets.SelectMode]::SelectRead) -and $client.Client.Available -eq 0) {
                    $alive = $false
                }
                return $alive
            }

            foreach ($step in $PlayList) {
                if ($State.Abort) { return }
                if ($step.ContainsKey('DelayMs')) {
                    Start-Sleep -Milliseconds ([int]$step.DelayMs)
                    continue
                }
                if ($step.ContainsKey('Send')) {
                    $data = [byte[]]$step.Send
                    $stream.Write($data, 0, $data.Length)
                    $stream.Flush()
                    continue
                }
                if ($step.ContainsKey('Expect')) {
                    $timeoutMs = 10000
                    if ($step.ContainsKey('TimeoutMs')) { $timeoutMs = [int]$step.TimeoutMs }
                    $until = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
                    $matched = $false
                    while ($true) {
                        $alive = & $pump
                        $all = $State.Received.ToArray()
                        $text = $latin1.GetString($all, $mark, $all.Length - $mark)
                        $m = [regex]::Match($text, [string]$step.Expect)
                        if ($m.Success) { $mark = $mark + $m.Index + $m.Length; $matched = $true; break }
                        if (-not $alive) {
                            $State.Failure = "peer closed while waiting for /" + $step.Expect + "/; got: " + ($text -replace "[^\x20-\x7E]", ".")
                            return
                        }
                        if ($State.Abort) { return }
                        if ([DateTime]::UtcNow -gt $until) {
                            $State.Failure = "timed out waiting for /" + $step.Expect + "/; got: " + ($text -replace "[^\x20-\x7E]", ".")
                            return
                        }
                        Start-Sleep -Milliseconds 20
                    }
                }
            }
            # Script done: keep recording so a case can see what else the app sends.
            while (-not $State.Abort) {
                [void](& $pump)
                Start-Sleep -Milliseconds 50
            }
        } catch {
            $State.Failure = "responder error: " + $_.Exception.Message
        } finally {
            if ($client) { try { $client.Close() } catch { } }
        }
    }

    $ps = [powershell]::Create()
    [void]$ps.AddScript($work)
    [void]$ps.AddArgument($listener)
    [void]$ps.AddArgument($Steps)
    [void]$ps.AddArgument($state)
    $handle = $ps.BeginInvoke()

    return @{ Port = $port; Handle = $handle; PS = $ps; Listener = $listener; State = $state; Stopped = $false; Bytes = [byte[]]@() }
}

function Stop-TelnetResponder {
    <# Stop the responder and return every byte it received. Safe to call twice.
       The recorded failure, if any, is left in $Responder.State.Failure. #>
    param([Parameter(Mandatory)] $Responder)
    if ($Responder.Stopped) { return ,$Responder.Bytes }
    Start-Sleep -Milliseconds 300          # let late bytes arrive before the recorder stops
    $Responder.State.Abort = $true
    [void]$Responder.Handle.AsyncWaitHandle.WaitOne(5000)
    try { [void]$Responder.PS.EndInvoke($Responder.Handle) } catch { }
    try { $Responder.PS.Dispose() } catch { }
    try { $Responder.Listener.Stop() } catch { }
    $Responder.Bytes = [byte[]]($Responder.State.Received.ToArray())
    $Responder.Stopped = $true
    return ,$Responder.Bytes
}

function Assert-TelnetResponderOk {
    <# Fail the case if the script's Expect steps did not all see their bytes. #>
    param([Parameter(Mandatory)] $Responder)
    Assert-True ([string]::IsNullOrEmpty($Responder.State.Failure)) ("the scripted device saw something unexpected: " + $Responder.State.Failure)
}

function Get-TelnetTerminalText {
    <# The terminal's text: Edit > Select All copies it to the clipboard. #>
    param([Parameter(Mandatory)] $Session)
    Set-Clipboard -Value "nutshell-no-selection"
    Send-NutshellCommand -Session $Session -Id $script:IDM_EDIT_SELECT_ALL_TN -SettleMs 500
    $t = Get-Clipboard -Raw
    if ($null -eq $t) { return "" }
    return [string]$t
}

function Wait-TelnetTerminalText {
    <# Wait until the terminal text matches -Pattern (regex); returns $true/$false. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Pattern, [int] $TimeoutSec = 10)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ((Get-TelnetTerminalText -Session $Session) -match $Pattern) { return $true }
        Start-Sleep -Milliseconds 300
    }
    return $false
}

function Test-TelnetErrorBoxOpen {
    <# $true when a top-level "Connection Error" window of the app exists. #>
    param([Parameter(Mandatory)] $Session)
    foreach ($w in (Get-NutshellWindows -Session $Session)) {
        if (($w -split "`t")[2] -match "Connection Error") { return $true }
    }
    return $false
}

function Invoke-TelnetCase {
    <# Start a responder (unless -Steps is $null, which hands the case a port
       nothing listens on), build a telnet profile "tn" on it, and run the case
       through Invoke-Case connecting by name. The body finds the responder in
       $script:TelnetCurrent and the port in $script:TelnetPort. #>
    param([Parameter(Mandatory)] [string] $Name, [hashtable] $Settings = @{},
          [hashtable[]] $Steps = $null, [hashtable] $Credentials = @{},
          [Parameter(Mandatory)] [scriptblock] $Body)
    if ($ActiveTiers -notcontains "gate") { return }
    if ($Only.Count -gt 0 -and $Only -notcontains $Name) { return }

    $responder = $null
    if ($null -ne $Steps) {
        $responder = Start-TelnetResponder -Steps $Steps
        $port = $responder.Port
    } else {
        # A port that was open a moment ago and is closed now: connect is refused.
        $l = [System.Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
        $l.Start()
        $port = ([Net.IPEndPoint]$l.LocalEndpoint).Port
        $l.Stop()
    }
    $script:TelnetCurrent = $responder
    $script:TelnetPort = $port

    $profile = @{ name = "tn"; kind = "telnet"; host = "127.0.0.1"; port = $port }
    foreach ($k in $Credentials.Keys) { $profile[$k] = $Credentials[$k] }
    try {
        Invoke-Case $Name $Settings $Body -ExtraArgs @("-sn", "tn") -ExtraProfiles @($profile)
    } finally {
        if ($null -ne $responder) { [void](Stop-TelnetResponder -Responder $responder) }
        $script:TelnetCurrent = $null
    }
}

$script:TelnetDebugSettings = @{
    debug_enabled = $true; debug_level_ssh = "debug"; debug_level_term = "trace"
    debug_include_content = $true; debug_log_dir = "debuglogs"
}

# ---- telnet_autologin_never_logs_password -------------------------------------
# Options negotiation, auto-login at "Username:" and "Password:", and the
# password staying out of every log.
$tnIac = [byte[]](0xFF,0xFB,0x01, 0xFF,0xFB,0x03, 0xFF,0xFD,0x1F, 0xFF,0xFD,0x18)   # WILL ECHO, WILL SGA, DO NAWS, DO TTYPE
$tnSteps1 = @(
    @{ DelayMs = 1500 },
    @{ Send = [byte[]]($tnIac + (ConvertTo-TelnetBytes "`r`nUsername: ")) },
    @{ Expect = 'ituser\r\n'; TimeoutMs = 15000 },
    @{ Send = (ConvertTo-TelnetBytes "ituser`r`n") },
    @{ Send = (ConvertTo-TelnetBytes "Password: ") },
    @{ Expect = 'itpass9\r\n'; TimeoutMs = 15000 },
    @{ Send = (ConvertTo-TelnetBytes "`r`nRouter>") }
)
Invoke-TelnetCase -Name "telnet_autologin_never_logs_password" -Settings $script:TelnetDebugSettings `
    -Steps $tnSteps1 -Credentials @{ username = "ituser"; password = "itpass9" } -Body {
    param($s)
    $r = $script:TelnetCurrent
    $port = $script:TelnetPort

    Start-NutshellLogging -Session $s | Out-Null
    Assert-True (Wait-NutshellLog -Session $s -Pattern 'Router>' -TimeoutSec 25) `
        "the device's 'Router>' prompt never reached the session log -- the auto-login did not complete"

    $got = Stop-TelnetResponder -Responder $r
    Assert-TelnetResponderOk -Responder $r
    Assert-True (Test-TelnetBytesContain $got ([byte[]](0xFF,0xFB,0x1F,0xFF,0xFA,0x1F))) `
        "the app did not answer DO NAWS with WILL NAWS followed by the window-size subnegotiation"
    Assert-True (Test-TelnetBytesContain $got ([byte[]](0xFF,0xFB,0x18))) `
        "the app did not answer DO TERMINAL-TYPE with WILL TERMINAL-TYPE"

    # The status line goes to the terminal only (not the session log).
    Assert-True (Wait-TelnetTerminalText -Session $s -Pattern ("Connected to 127\.0\.0\.1:" + $port + " \(telnet") -TimeoutSec 5) `
        "the tab never showed 'Connected to 127.0.0.1:$port (telnet ...'"

    $log = Get-NutshellLogText -Session $s
    Assert-True ($log -match [regex]::Escape("Router>")) "the session log lacks the device prompt"
    Assert-True ($log -notmatch 'itpass9') "the session log contains the telnet password"

    Assert-True (Wait-NutshellDebugLog -Session $s -Pattern 'telnet\.connect\.done' -TimeoutSec 5) `
        "the debug log has no telnet.connect.done event (debug logging did not run, so the next check would be vacuous)"
    # telnet.connect.target is logged only when content logging is on, so its
    # presence proves the content-gated events ran and the absence check means something.
    Assert-True (Wait-NutshellDebugLog -Session $s -Pattern 'telnet\.connect\.target' -TimeoutSec 5) `
        "the debug log has no telnet.connect.target event (content logging did not run, so the next check would be vacuous)"
    Assert-True ((Get-NutshellDebugLogText -Session $s) -notmatch 'itpass9') "the debug log contains the telnet password"
    "negotiated NAWS and TTYPE, logged in at Username:/Password:, reached Router>; the password is in neither the session log nor the debug log"
}

# ---- telnet_port_refused_reports_in_tab ---------------------------------------
Invoke-TelnetCase -Name "telnet_port_refused_reports_in_tab" -Settings $script:TelnetDebugSettings -Body {
    param($s)
    $port = $script:TelnetPort
    Assert-True (Wait-TelnetTerminalText -Session $s -Pattern ("Connection refused by 127\.0\.0\.1:" + $port) -TimeoutSec 15) `
        "the tab never showed 'Connection refused by 127.0.0.1:$port'"
    Assert-True (Wait-NutshellDebugLog -Session $s -Pattern 'telnet\.connect\.fail .*outcome=refused' -TimeoutSec 5) `
        "the debug log has no telnet.connect.fail outcome=refused event"
    Start-Sleep -Milliseconds 1000
    Assert-True (-not (Test-TelnetErrorBoxOpen -Session $s)) `
        "a 'Connection Error' message box opened; a telnet connect failure is reported in the tab only"
    "a refused telnet connect printed 'Connection refused by 127.0.0.1:$port' in the tab, logged outcome=refused, and opened no message box"
}

# ---- telnet_port_check_is_passive_and_echoes_locally ---------------------------
# No username and no password: a port check. Nothing is sent but what is typed,
# no option negotiation goes out, and the typing is echoed locally.
$tnSteps3 = @(
    @{ DelayMs = 1500 },
    @{ Send = (ConvertTo-TelnetBytes "220 ready`r`n") },
    @{ Expect = 'HELO it\r\n'; TimeoutMs = 20000 }
)
Invoke-TelnetCase -Name "telnet_port_check_is_passive_and_echoes_locally" -Steps $tnSteps3 -Body {
    param($s)
    $r = $script:TelnetCurrent
    Start-NutshellLogging -Session $s | Out-Null
    Assert-True (Wait-NutshellLog -Session $s -Pattern '220 ready' -TimeoutSec 15) `
        "the device's banner never reached the session log"
    Send-NutshellLine -Session $s -Line "HELO it"
    Assert-True (Wait-NutshellLog -Session $s -Pattern 'HELO it' -TimeoutSec 10) `
        "the typed line was not echoed locally into the session log"
    Start-Sleep -Milliseconds 500
    $got = Stop-TelnetResponder -Responder $r
    Assert-TelnetResponderOk -Responder $r
    Assert-True (-not (Test-TelnetBytesContain $got ([byte[]](0xFF)))) `
        "the app sent an IAC byte (0xFF) on a plain port check: it must not negotiate options unasked"
    "a credential-less profile sent only 'HELO it' CRLF (no 0xFF bytes) and echoed it locally"
}

# ---- telnet_password_box_asks_and_sends ---------------------------------------
# A username and no saved password: the box opens at the device's password
# prompt; the typed password is sent, and neither logged nor saved (tick off).
$tnSteps4 = @(
    @{ DelayMs = 1500 },
    @{ Send = (ConvertTo-TelnetBytes "login: ") },
    @{ Expect = 'ituser\r\n'; TimeoutMs = 15000 },
    @{ Send = (ConvertTo-TelnetBytes "ituser`r`nPassword: ") },
    @{ Expect = 'boxpass1\r\n'; TimeoutMs = 40000 },
    @{ Send = (ConvertTo-TelnetBytes "`r`n`$ ") }
)
Invoke-TelnetCase -Name "telnet_password_box_asks_and_sends" -Steps $tnSteps4 -Credentials @{ username = "ituser" } -Body {
    param($s)
    $r = $script:TelnetCurrent
    Start-NutshellLogging -Session $s | Out-Null

    $dlg = Wait-NutshellDialog -Session $s -Title '^Password$' -TimeoutSec 25
    Assert-True ($dlg -ne [IntPtr]::Zero) "no 'Password' box appeared at the device's password prompt"
    $edit = Get-NutshellControl -Dialog $dlg -Id $script:PW_ID_EDIT_TN
    Assert-True ($edit -ne [IntPtr]::Zero) "the Password box has no edit field (id $($script:PW_ID_EDIT_TN))"
    Set-NutshellControlText -Control $edit -Text "boxpass1"
    $ok = Get-NutshellControl -Dialog $dlg -Id 1       # IDOK
    Assert-True ($ok -ne [IntPtr]::Zero) "the Password box has no OK button"
    Invoke-NutshellButton -Control $ok

    Assert-True (Wait-NutshellLog -Session $s -Pattern '\$ ' -TimeoutSec 20) `
        "the device's shell prompt never arrived after the password was entered"
    $got = Stop-TelnetResponder -Responder $r
    Assert-TelnetResponderOk -Responder $r

    Start-Sleep -Milliseconds 1000
    Assert-True ((Get-NutshellLogText -Session $s) -notmatch 'boxpass1') "the session log contains the typed password"
    $cfgPath = Join-Path $s.Env.Root "nutshell.config"
    # config_save writes secrets DPAPI-encrypted, so plaintext never appears; assert
    # on the tn profile's stored password field instead: it must still be empty.
    $cfgJson = [IO.File]::ReadAllText($cfgPath) | ConvertFrom-Json
    $tnProf = @($cfgJson.profiles | Where-Object { $_.name -eq "tn" })
    Assert-True ($tnProf.Count -eq 1) "nutshell.config has no single 'tn' profile to check"
    Assert-True ([string]$tnProf[0].password -eq "") "the tn profile gained a stored password although 'Save password to this profile' was not ticked"
    "the Password box opened at the prompt, its text reached the device, and the password is in neither the session log nor nutshell.config"
}

# ---- ssh_password_box_cancel_reports_in_tab -----------------------------------
# An SSH password-auth profile with nothing saved asks with the same box;
# Cancel ends the attempt quietly, in the tab, with no message box. Needs the
# real SSH host the driver was given (it only has to reach the auth stage).
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "ssh_password_box_cancel_reports_in_tab")) {
    $sshProfile = @{
        name = "sp"; kind = "ssh"; host = $HostName; port = 22; username = $User
        auth_type = "password"; password = ""
    }
    Invoke-Case "ssh_password_box_cancel_reports_in_tab" $script:TelnetDebugSettings {
        param($s)
        $dlg = Wait-NutshellDialog -Session $s -Title 'SSH Password' -TimeoutSec 30
        Assert-True ($dlg -ne [IntPtr]::Zero) "no 'SSH Password' box appeared for a password-auth profile with no saved password"
        $cancel = Get-NutshellControl -Dialog $dlg -Id 2     # IDCANCEL
        Assert-True ($cancel -ne [IntPtr]::Zero) "the SSH Password box has no Cancel button"
        Invoke-NutshellButton -Control $cancel
        Assert-True (Wait-TelnetTerminalText -Session $s -Pattern 'Password entry cancelled\.' -TimeoutSec 10) `
            "the tab never showed 'Password entry cancelled.'"
        Start-Sleep -Milliseconds 1000
        Assert-True (-not (Test-TelnetErrorBoxOpen -Session $s)) `
            "a 'Connection Error' message box opened after the user cancelled the password box"
        "Cancel in the SSH Password box printed 'Password entry cancelled.' in the tab and opened no error box"
    } -ExtraArgs @("-sn", "sp") -ExtraProfiles @($sshProfile)
}
