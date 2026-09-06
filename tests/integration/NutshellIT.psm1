# NutshellIT.psm1 — integration-test helpers for driving the real nutshell.exe
# against a live SSH host. Windows PowerShell 5.1 compatible.
#
# Mechanics:
#   * A scratch directory gets a copy of nutshell.exe and a generated
#     nutshell.config holding one key-auth profile for the target host.
#   * The app is launched with `-sn <profile>` so it connects on startup.
#   * Session logging is switched on through the File menu command, and every
#     assertion reads the ANSI-stripped session log — no OCR, no screen reads.
#   * Keystrokes go through WScript.Shell.SendKeys after the main window is
#     brought to the foreground; screenshots use PrintWindow for evidence.

Set-StrictMode -Version 2

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public class NutshellNative {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    public static List<string> ListWindows(uint pid) {
        var o = new List<string>();
        EnumWindows((h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid && IsWindowVisible(h)) {
                var c = new StringBuilder(256); GetClassName(h, c, 256);
                var t = new StringBuilder(256); GetWindowText(h, t, 256);
                o.Add(string.Format("{0}\t{1}\t{2}", (long)h, c, t));
            }
            return true; }, IntPtr.Zero);
        return o;
    }
}
"@

[NutshellNative]::SetProcessDPIAware() | Out-Null

# WM_COMMAND ids from src/ui/resource.h
$script:WM_COMMAND        = 0x0111
$script:WM_CLOSE          = 0x0010
$script:IDM_FILE_LOG_START = 2004
$script:IDM_FILE_LOG_STOP  = 2005
$script:IDM_FILE_CONNECT   = 2002

function New-NutshellTestEnv {
    <#
    .SYNOPSIS
        Create a scratch directory with nutshell.exe and a generated config.
    .PARAMETER Exe       Path to the built nutshell.exe.
    .PARAMETER HostName  SSH host to connect to.
    .PARAMETER User      SSH user name.
    .PARAMETER KeyPath   Private key file (must be passphrase-free).
    .PARAMETER Settings  Hashtable of extra top-level settings to override, e.g.
                         @{ paste_confirm = $false; log_format = "%Y%m%d-%H%M" }.
    #>
    param(
        [Parameter(Mandatory)] [string] $Exe,
        [Parameter(Mandatory)] [string] $HostName,
        [Parameter(Mandatory)] [string] $User,
        [Parameter(Mandatory)] [string] $KeyPath,
        [string] $ProfileName = "it",
        [hashtable] $Settings = @{}
    )
    $root = Join-Path $env:TEMP ("nutshell-it-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
    $logs = Join-Path $root "logs"
    New-Item -ItemType Directory -Force $root | Out-Null
    New-Item -ItemType Directory -Force $logs | Out-Null
    Copy-Item $Exe (Join-Path $root "nutshell.exe") -Force

    $s = @{
        font = "Consolas"; ai_font = "Consolas"; font_size = 10
        scrollback_lines = 10000; paste_delay_ms = 0
        logging_enabled = $false; debug_terminal = $false
        log_format = "%Y-%m-%d_%H-%M-%S"; log_dir = $logs
        host_key_verification = "tofu"
        foreground_colour = "#E0E0E0"; background_colour = "#121212"
        colour_scheme = "Onyx Synapse"
        ai_provider = "anthropic"; ai_custom_url = ""; ai_custom_model = ""
        ai_api_key = ""; ai_system_notes = ""
        ai_search_provider = "none"; ai_search_url = ""; ai_max_search_results = 7
        ai_web_fetch_enabled = $false; ssh_user_idle_timeout_mins = 0
        markdown_render_enabled = $true; ai_max_context_lines = 1000
        auto_connect = $false; auto_connect_session = ""
        paste_confirm = $true; open_session_manager_at_start = $false
        ai_auto_approve_all = $false
    }
    foreach ($k in $Settings.Keys) { $s[$k] = $Settings[$k] }

    $profile = [ordered]@{
        name = $ProfileName; host = $HostName; port = 22; username = $User
        auth_type = "key"; password = ""; key_path = $KeyPath; ai_notes = ""
    }
    $cfg = [ordered]@{ settings = $s; profiles = @($profile) }
    $json = $cfg | ConvertTo-Json -Depth 5
    [IO.File]::WriteAllText((Join-Path $root "nutshell.config"), $json, (New-Object Text.UTF8Encoding $false))

    return [pscustomobject]@{ Root = $root; Logs = $logs; Exe = (Join-Path $root "nutshell.exe"); ProfileName = $ProfileName }
}

function Start-Nutshell {
    <# Launch the app connected to the env's profile; returns a session object. #>
    param([Parameter(Mandatory)] $Env, [int] $TimeoutSec = 15)
    $p = Start-Process -FilePath $Env.Exe -WorkingDirectory $Env.Root -ArgumentList @("-sn", $Env.ProfileName) -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $main = [IntPtr]::Zero
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 300
        $p.Refresh()
        $w = [NutshellNative]::ListWindows([uint32]$p.Id) | Where-Object { $_ -match "`tNutshell_Window`t" } | Select-Object -First 1
        if ($w) { $main = [IntPtr][long]($w -split "`t")[0]; break }
    }
    if ($main -eq [IntPtr]::Zero) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; throw "Nutshell main window did not appear" }
    return [pscustomobject]@{ Process = $p; Main = $main; Env = $Env; Log = $null }
}

function Stop-Nutshell {
    param([Parameter(Mandatory)] $Session)
    if ($Session.Process -and -not $Session.Process.HasExited) {
        Stop-Process -Id $Session.Process.Id -Force -ErrorAction SilentlyContinue
    }
}

function Get-NutshellWindows {
    param([Parameter(Mandatory)] $Session)
    return [NutshellNative]::ListWindows([uint32]$Session.Process.Id)
}

function Send-NutshellCommand {
    <# Post a WM_COMMAND menu id to the main window. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [int] $Id, [int] $SettleMs = 500)
    [NutshellNative]::PostMessage($Session.Main, $script:WM_COMMAND, [IntPtr]$Id, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds $SettleMs
}

function Start-NutshellLogging {
    <# Turn on session logging and return the log file path once it exists. #>
    param([Parameter(Mandatory)] $Session, [int] $TimeoutSec = 10)
    $before = @(Get-ChildItem -Path $Session.Env.Logs -Filter *.log -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
    Send-NutshellCommand -Session $Session -Id $script:IDM_FILE_LOG_START
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $now = @(Get-ChildItem -Path $Session.Env.Logs -Filter *.log -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
        $new = $now | Where-Object { $before -notcontains $_ }
        if ($new) { $Session.Log = ($new | Select-Object -First 1); return $Session.Log }
        Start-Sleep -Milliseconds 250
    }
    throw "No session log appeared in $($Session.Env.Logs) within ${TimeoutSec}s (is the session connected?)"
}

function Send-NutshellKeys {
    <# SendKeys syntax: ^c = Ctrl+C, ^+c = Ctrl+Shift+C, {PGUP}, {ENTER} ... #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Keys, [int] $SettleMs = 300)
    [NutshellNative]::SetForegroundWindow($Session.Main) | Out-Null
    Start-Sleep -Milliseconds 150
    $ws = New-Object -ComObject WScript.Shell
    $ws.SendKeys($Keys)
    Start-Sleep -Milliseconds $SettleMs
}

function Send-NutshellLine {
    <# Type a shell command line and press Enter. Braces/plus/caret are escaped for SendKeys. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Line, [int] $SettleMs = 300)
    $escaped = ($Line -replace '([+^%~(){}\[\]])', '{$1}')
    Send-NutshellKeys -Session $Session -Keys ($escaped + "{ENTER}") -SettleMs $SettleMs
}

function Get-NutshellLogText {
    param([Parameter(Mandatory)] $Session)
    if (-not $Session.Log -or -not (Test-Path $Session.Log)) { return "" }
    # Open with sharing so we can read while the app holds the file open.
    $fs = [IO.File]::Open($Session.Log, 'Open', 'Read', 'ReadWrite')
    try { $sr = New-Object IO.StreamReader($fs); return $sr.ReadToEnd() } finally { $fs.Dispose() }
}

function Wait-NutshellLog {
    <# Wait until the session log matches -Pattern (regex). Returns $true/$false. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Pattern, [int] $TimeoutSec = 10)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ((Get-NutshellLogText -Session $Session) -match $Pattern) { return $true }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

function Wait-NutshellShell {
    <# Press Enter until a shell prompt ($ or #) shows up in the log. Throws on timeout. #>
    param([Parameter(Mandatory)] $Session, [int] $TimeoutSec = 25)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Send-NutshellKeys -Session $Session -Keys "{ENTER}" -SettleMs 700
        if ((Get-NutshellLogText -Session $Session) -match '[$#]\s*$') { return }
    }
    throw "no shell prompt within ${TimeoutSec}s (still connecting, or auth failed)"
}

function Set-NutshellWindowSize {
    param([Parameter(Mandatory)] $Session, [int] $X = 40, [int] $Y = 40, [Parameter(Mandatory)] [int] $Width, [Parameter(Mandatory)] [int] $Height)
    [NutshellNative]::SetWindowPos($Session.Main, [IntPtr]::Zero, $X, $Y, $Width, $Height, 0x0004) | Out-Null
    Start-Sleep -Milliseconds 600
}

function Save-NutshellScreenshot {
    <# PrintWindow capture of the main window (or -Hwnd) to a PNG; returns the path. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Path, [long] $Hwnd = 0)
    $h = $Session.Main
    if ($Hwnd -ne 0) { $h = [IntPtr]$Hwnd }
    $r = New-Object NutshellNative+RECT
    [NutshellNative]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $hh = $r.B - $r.T
    if ($w -le 0 -or $hh -le 0) { throw "window has no size" }
    $bmp = New-Object Drawing.Bitmap $w, $hh
    $g = [Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [NutshellNative]::PrintWindow($h, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc)
    $dir = Split-Path $Path -Parent
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
    $bmp.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    return $Path
}

Export-ModuleMember -Function New-NutshellTestEnv, Start-Nutshell, Stop-Nutshell, Get-NutshellWindows, `
    Send-NutshellCommand, Start-NutshellLogging, Send-NutshellKeys, Send-NutshellLine, `
    Get-NutshellLogText, Wait-NutshellLog, Wait-NutshellShell, Set-NutshellWindowSize, Save-NutshellScreenshot
