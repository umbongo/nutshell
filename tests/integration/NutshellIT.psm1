# NutshellIT.psm1 — integration-test helpers for driving the real nutshell.exe
# against a live SSH host. Windows PowerShell 5.1 compatible.
#
# Mechanics:
#   * A scratch directory gets a copy of nutshell.exe and a generated
#     nutshell.config holding one key-auth profile for the target host.
#   * The app is launched with `-sn <profile>` so it connects on startup.
#   * Session logging is switched on through the File menu command, and every
#     assertion reads the ANSI-stripped session log — no OCR, no screen reads.
#   * Text and named keys (Send-NutshellText/-Key/-Line, Wait-NutshellShell) go
#     through PostMessage(WM_CHAR / WM_KEYDOWN+WM_KEYUP) straight to the main
#     window's queue — no foreground window, no focus, and no unlocked desktop
#     required. Only true modifier chords the app reads via GetKeyState
#     (Ctrl+C/V, Ctrl+Shift+C/V, Shift+Insert, Ctrl+= zoom) still need
#     Send-NutshellKeys' real SendKeys input, which needs the foreground.
#   * Dialogs (Session Manager, Settings, paste preview, passphrase prompt,
#     host-key/error MessageBoxes, About) are driven the same posted way:
#     GetDlgItem/EnumChildWindows to find a control by its resource id, then
#     WM_SETTEXT / BM_CLICK / CB_*/LB_* messages — see the "Dialog helpers"
#     section below.
#   * Screenshots use PrintWindow for evidence; Get-NutshellPixel/
#     Test-NutshellPixelNear/Get-NutshellThemeColor turn a capture into an
#     oracle by comparing sampled pixels against src/core/ui_theme.c's tokens.

Set-StrictMode -Version 2

Add-Type -AssemblyName System.Drawing
# UIA is loaded per the brief's dialog-helper design (EnumChildWindows/GetDlgCtrlID
# as the documented fallback for controls UIA does not expose). In practice every
# Nutshell dialog turned out to be a plain resource dialog (Session Manager) or a
# hand-built child-window dialog (Settings, paste preview, passphrase prompt,
# About) with real Win32 control IDs on every control -- confirmed by reading
# resource.rc / settings.c / paste_dlg.c / window.c -- so the concrete helpers
# below use GetDlgItem + the recursive EnumChildWindows fallback throughout and
# never needed to walk the UIA tree. The assemblies stay loaded (best-effort) so
# a future helper that needs them (e.g. a dialog with no real control ID) has
# them available without another Add-Type call.
try {
    Add-Type -AssemblyName UIAutomationClient -ErrorAction Stop
    Add-Type -AssemblyName UIAutomationTypes -ErrorAction Stop
} catch {
    Write-Warning "UIAutomationClient/Types not available ($($_.Exception.Message)); dialog helpers still work via Win32 control IDs."
}
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public class NutshellNative {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", EntryPoint="PostMessageW")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMsg(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr SendMsgSb(IntPtr h, uint m, IntPtr w, StringBuilder sb);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint uCode, uint uMapType);
    /* title is IntPtr so callers can pass Zero: PowerShell would turn a $null string into "" (match empty titles only). */
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, IntPtr title);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr SendMsgStr(IntPtr h, uint m, IntPtr w, string l);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("kernel32.dll")] public static extern IntPtr GetStdHandle(int nStdHandle);
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct COORD { public short X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct SMALL_RECT { public short Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct CONSOLE_SCREEN_BUFFER_INFO {
        public COORD dwSize; public COORD dwCursorPosition; public ushort wAttributes;
        public SMALL_RECT srWindow; public COORD dwMaximumWindowSize;
    }
    [DllImport("kernel32.dll")] public static extern bool GetConsoleScreenBufferInfo(IntPtr h, out CONSOLE_SCREEN_BUFFER_INFO info);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern bool ReadConsoleOutputCharacterW(IntPtr h, StringBuilder buf, uint n, COORD coord, out uint read);

    /* Read the trailing N screen rows of the active console this process is
     * attached to. Used to recover the text nutshell.exe -v prints: it calls
     * AttachConsole(ATTACH_PARENT_PROCESS) then freopen("CONOUT$", ...)
     * (src/main.c's cli_output), which opens the console's *active screen
     * buffer* directly -- distinct from GetStdHandle(STD_OUTPUT_HANDLE),
     * which this harness process may have had redirected to a pipe by
     * whatever launched it, even though it (and its child nutshell.exe,
     * which inherits the attachment) are still attached to a real console
     * underneath. Opening "CONOUT$" the same way cli_output does -- instead
     * of GetStdHandle -- reads that same active screen buffer regardless of
     * what our own STD_OUTPUT_HANDLE points to. */
    public static string ReadConsoleTail(int lines) {
        IntPtr h = CreateFileW("CONOUT$", 0xC0000000 /* GENERIC_READ|WRITE */,
            3 /* FILE_SHARE_READ|WRITE */, IntPtr.Zero, 3 /* OPEN_EXISTING */, 0, IntPtr.Zero);
        if (h == IntPtr.Zero || h == new IntPtr(-1)) return "";
        try {
            CONSOLE_SCREEN_BUFFER_INFO info;
            if (!GetConsoleScreenBufferInfo(h, out info)) return "";
            int width = info.dwSize.X;
            int curY = info.dwCursorPosition.Y;
            int startY = Math.Max(0, curY - lines + 1);
            var sb = new StringBuilder();
            for (int y = startY; y <= curY; y++) {
                var line = new StringBuilder(width);
                COORD coord; coord.X = 0; coord.Y = (short)y;
                uint read;
                ReadConsoleOutputCharacterW(h, line, (uint)width, coord, out read);
                sb.Append(line.ToString());
                sb.Append('\n');
            }
            return sb.ToString();
        } finally {
            CloseHandle(h);
        }
    }

    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    /* Descendant windows of root (recursive -- EnumChildWindows walks the
     * whole subtree, not just direct children), as hwnd\tctrlId\tclass\ttext. */
    public static List<string> ListChildren(IntPtr root) {
        var o = new List<string>();
        EnumChildWindows(root, (h, l) => {
            var c = new StringBuilder(256); GetClassName(h, c, 256);
            var t = new StringBuilder(256); GetWindowText(h, t, 256);
            int id = GetDlgCtrlID(h);
            o.Add(string.Format("{0}\t{1}\t{2}\t{3}", (long)h, id, c, t));
            return true; }, IntPtr.Zero);
        return o;
    }

    /* First descendant (any depth) with the given control id, or Zero. */
    public static IntPtr FindChildById(IntPtr root, int id) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(root, (h, l) => {
            if (GetDlgCtrlID(h) == id) { found = h; return false; }
            return true; }, IntPtr.Zero);
        return found;
    }

    /* Left-click at an offset from the window's top-left corner (physical px). */
    public static void ClickAt(IntPtr h, int ox, int oy) {
        RECT r; GetWindowRect(h, out r);
        SetCursorPos(r.L + ox, r.T + oy); System.Threading.Thread.Sleep(100);
        mouse_event(0x0002, 0, 0, 0, IntPtr.Zero); System.Threading.Thread.Sleep(60);
        mouse_event(0x0004, 0, 0, 0, IntPtr.Zero);
    }

    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

    /* Client size of a window (physical px). */
    public static RECT ClientSize(IntPtr h) { RECT r; GetClientRect(h, out r); return r; }

    /* Left-click at client coordinates — independent of title bar / menu bar
     * height, which differ between DPIs and Windows versions. */
    public static void ClickClient(IntPtr h, int cx, int cy) {
        POINT p; p.X = cx; p.Y = cy; ClientToScreen(h, ref p);
        SetCursorPos(p.X, p.Y); System.Threading.Thread.Sleep(100);
        mouse_event(0x0002, 0, 0, 0, IntPtr.Zero); System.Threading.Thread.Sleep(60);
        mouse_event(0x0004, 0, 0, 0, IntPtr.Zero);
    }

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
$script:IDM_FILE_NEW_SESSION = 2001
$script:IDM_EDIT_SETTINGS    = 2013
$script:IDC_SETTINGS_NAV     = 3069
$script:IDC_LIST_SESSIONS    = 1000

# Posted-message constants (window.c's WM_CHAR/WM_KEYDOWN handlers; see
# Send-NutshellText/-Key below for which VKs are handled directly vs. rely on
# TranslateMessage to synthesize the WM_CHAR, mirroring what real typing does).
$script:WM_CHAR            = 0x0102
$script:WM_KEYDOWN         = 0x0100
$script:WM_KEYUP           = 0x0101
$script:WM_SETTEXT         = 0x000C
$script:WM_GETTEXTLENGTH   = 0x000E
$script:BM_CLICK           = 0x00F5
$script:BM_SETCHECK        = 0x00F1
$script:BM_GETCHECK        = 0x00F0
$script:BST_CHECKED        = 1
$script:CB_GETCURSEL       = 0x0147
$script:CB_SETCURSEL       = 0x014E
$script:CB_FINDSTRINGEXACT = 0x0158
$script:LB_GETCURSEL       = 0x0188
$script:LB_SETCURSEL       = 0x0186
$script:LB_GETCOUNT        = 0x018B
$script:LB_GETTEXT         = 0x0189
$script:LB_GETTEXTLEN      = 0x018A
$script:EN_CHANGE          = 0x0300
$script:CBN_SELCHANGE      = 1
$script:LBN_SELCHANGE      = 1

# Named keys for Send-NutshellKey: VK code and whether it's an "extended" key
# (sets bit 24 of the posted lParam, as a real keyboard would for the grey
# nav-cluster keys). Enter/Tab/Escape/Backspace are not handled specially in
# window.c's WM_KEYDOWN switch (see the "Let DefWindowProc generate WM_CHAR
# for unhandled keys" comment there) -- posting WM_KEYDOWN+WM_KEYUP for them
# relies on ui_run()'s TranslateMessage(&msg) (window.c, no accelerator table
# or IsDialogMessage filtering) to synthesize the matching WM_CHAR, exactly as
# real typing does. PgUp/PgDn/Home/End/arrows/Insert/F-keys ARE handled
# directly in that switch and never produce a WM_CHAR either way.
$script:VK_MAP = @{
    Enter = 0x0D; Tab = 0x09; Escape = 0x1B; Backspace = 0x08
    PgUp = 0x21; PgDn = 0x22; Home = 0x24; End = 0x23
    Up = 0x26; Down = 0x28; Left = 0x25; Right = 0x27; Insert = 0x2D
    F1 = 0x70; F2 = 0x71; F3 = 0x72; F4 = 0x73; F5 = 0x74; F6 = 0x75
    F7 = 0x76; F8 = 0x77; F9 = 0x78; F10 = 0x79; F11 = 0x7A; F12 = 0x7B
}
$script:VK_EXTENDED = @(0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E)

# Window classes Nutshell's own dialogs use (see src/ui/*.c for each
# RegisterClass call). Session Manager and the host-key/error MessageBoxes are
# real dialogs (class #32770); Settings, the paste preview, the passphrase
# prompt and About are hand-built popup windows with their own classes.
$script:DIALOG_CLASSES = @("#32770", "NutshellPassDlg", "Nutshell_Settings", "Nutshell_PastePreview", "Nutshell_About")

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
    # Scratch lives under the git-ignored artifacts folder, not %TEMP%, so a run
    # whose cleanup was interrupted leaves something visible next to its logs.
    $root = Join-Path $PSScriptRoot ("artifacts\scratch\" + [guid]::NewGuid().ToString("N").Substring(0, 8))
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
        ai_auto_approve_default = 0
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
    <#
    .SYNOPSIS
        Launch the app; returns a session object once the main window appears.
    .PARAMETER ExtraArgs
        When given, launched with these args verbatim instead of
        "-sn <profile>" -- e.g. @("--ui-demo=chat", "--theme", "Onyx Light")
        for the ui_gallery case, which needs no SSH profile at all.
    #>
    param([Parameter(Mandatory)] $Env, [int] $TimeoutSec = 15, [string[]] $ExtraArgs = @())
    $rawArgs = if ($ExtraArgs.Count -gt 0) { $ExtraArgs } else { @("-sn", $Env.ProfileName) }
    # Start-Process's -ArgumentList does not auto-quote array elements containing
    # spaces (Windows PowerShell 5.1), so an unquoted "Onyx Light" would arrive
    # as two argv entries and fail CLI parsing. Quote any element that needs it.
    $argList = $rawArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
    $p = Start-Process -FilePath $Env.Exe -WorkingDirectory $Env.Root -ArgumentList $argList -PassThru
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
        # Wait for the handle on the session log to go away so the scratch dir can be removed.
        try { $Session.Process.WaitForExit(5000) | Out-Null } catch { }
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

function New-NutshellKeyLParam {
    <# Build a plausible WM_KEYDOWN/WM_KEYUP lParam: real scan code (via
       MapVirtualKey), repeat count 1, the extended-key bit for the grey
       nav-cluster keys, and (for a key-up) the transition-state/prev-state
       bits a real key-up carries. Not exported -- internal to this module. #>
    param([Parameter(Mandatory)] [byte] $Vk, [bool] $Extended = $false, [bool] $KeyUp = $false)
    $scan = [NutshellNative]::MapVirtualKey([uint32]$Vk, 0)   # MAPVK_VK_TO_VSC
    $l = [uint32]1 -bor ([uint32]$scan -shl 16)
    if ($Extended) { $l = $l -bor 0x01000000 }
    if ($KeyUp) { $l = $l -bor 0x40000000 -bor 0x80000000 }
    return [IntPtr][int64]$l
}

function Send-NutshellText {
    <# Post each character of -Text as a WM_CHAR straight to the main window's
       message queue -- no foreground, no focus, no unlocked desktop required
       (see window.c's WM_CHAR handler: it writes wParam's byte to the SSH
       channel unconditionally, regardless of what has Win32 keyboard focus).
       A newline is sent as a posted Enter (Send-NutshellKey), since window.c
       relies on TranslateMessage to turn VK_RETURN into the WM_CHAR 0x0D that
       actually reaches the shell -- see Send-NutshellKey's comment. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Text, [int] $SettleMs = 50)
    foreach ($ch in $Text.ToCharArray()) {
        if ($ch -eq "`n" -or $ch -eq "`r") {
            Send-NutshellKey -Session $Session -Key Enter -SettleMs 0
        } else {
            [NutshellNative]::PostMessage($Session.Main, $script:WM_CHAR, [IntPtr][int]$ch, [IntPtr]::Zero) | Out-Null
            Start-Sleep -Milliseconds 5
        }
    }
    Start-Sleep -Milliseconds $SettleMs
}

function Send-NutshellKey {
    <# Post a named key (see $script:VK_MAP) as WM_KEYDOWN+WM_KEYUP straight to
       the main window's message queue. Enter/Tab/Escape/Backspace are not
       handled in window.c's WM_KEYDOWN switch, so what actually reaches the
       shell is the WM_CHAR that ui_run()'s TranslateMessage(&msg) synthesizes
       from the posted key pair (there's no accelerator table or
       IsDialogMessage filtering on the main loop, so this works exactly like
       a real keypress). PgUp/PgDn/Home/End/arrows/Insert/F-keys ARE handled
       directly in that switch and act on WM_KEYDOWN alone. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Key, [int] $SettleMs = 150)
    if (-not $script:VK_MAP.ContainsKey($Key)) {
        throw "Unknown key name '$Key' (known: $($script:VK_MAP.Keys -join ', '))"
    }
    $vk = [byte]$script:VK_MAP[$Key]
    $ext = $script:VK_EXTENDED -contains $vk
    $down = New-NutshellKeyLParam -Vk $vk -Extended $ext -KeyUp $false
    $up   = New-NutshellKeyLParam -Vk $vk -Extended $ext -KeyUp $true
    [NutshellNative]::PostMessage($Session.Main, $script:WM_KEYDOWN, [IntPtr]$vk, $down) | Out-Null
    [NutshellNative]::PostMessage($Session.Main, $script:WM_KEYUP,   [IntPtr]$vk, $up)   | Out-Null
    Start-Sleep -Milliseconds $SettleMs
}

function Send-NutshellKeys {
    <# SendKeys syntax: ^c = Ctrl+C, ^+c = Ctrl+Shift+C, {PGUP}, {ENTER} ...
       Real synthetic input (WScript.Shell.SendKeys), needed ONLY for the true
       modifier chords window.c reads via GetKeyState at the moment the
       message is processed rather than from the posted message itself --
       Ctrl+C/V, Ctrl+Shift+C/V, Shift+Insert, Ctrl+=/- zoom. Those need the
       Ctrl/Shift key to actually be down in the live keyboard state, which
       only real input (or keybd_event) can produce; a posted WM_KEYDOWN alone
       does not change GetKeyState's answer. That means this function -- and
       every case that still calls it -- needs the foreground and therefore an
       unlocked, interactive desktop; everything else in this module (typing,
       Enter, Tab, Escape, Backspace, arrows, Home/End, PgUp/PgDn, Insert,
       F-keys, and every dialog helper below) is posted and works desktop-locked. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Keys, [int] $SettleMs = 300)
    # SendKeys types into whatever window is in front, so Nutshell must own the
    # foreground first. Windows only lets a process take the foreground when it
    # recently received input, and this harness usually has not (it has been
    # sleeping, waiting for a shell or an AI reply), so a bare
    # SetForegroundWindow is silently ignored once someone else has the focus.
    # The standard workaround: tap Alt (keybd_event, no window receives a
    # character from it) so this process counts as the last input source, then
    # ask again. Try a few times, then refuse rather than type blind -- a locked
    # desktop reports no foreground window at all, and a person using the
    # machine may legitimately hold the foreground; keystrokes must never land
    # in someone else's window.
    $ok = $false
    for ($try = 0; $try -lt 4 -and -not $ok; $try++) {
        if ($try -gt 0) {
            [NutshellNative]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)        # Alt down
            [NutshellNative]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)        # Alt up (KEYEVENTF_KEYUP)
        }
        [NutshellNative]::SetForegroundWindow($Session.Main) | Out-Null
        Start-Sleep -Milliseconds 150
        $fg = [NutshellNative]::GetForegroundWindow()
        $fgPid = [uint32]0
        if ($fg -ne [IntPtr]::Zero) { [NutshellNative]::GetWindowThreadProcessId($fg, [ref]$fgPid) | Out-Null }
        $ok = ($fg -ne [IntPtr]::Zero -and [int]$fgPid -eq $Session.Process.Id)
    }
    if (-not $ok) {
        throw "Nutshell is not the foreground window (desktop locked, or another app holds the foreground); refusing to send keys"
    }
    $ws = New-Object -ComObject WScript.Shell
    $ws.SendKeys($Keys)
    Start-Sleep -Milliseconds $SettleMs
}

function Send-NutshellLine {
    <# Type a shell command line and press Enter -- fully posted (Send-NutshellText
       + Send-NutshellKey), so no escaping is needed for SendKeys-special
       characters and no foreground window is required. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Line, [int] $SettleMs = 300)
    Send-NutshellText -Session $Session -Text $Line -SettleMs 30
    Send-NutshellKey -Session $Session -Key Enter -SettleMs $SettleMs
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

function Open-NutshellSecondTab {
    <# Open the Session Manager, pick the first saved profile and Connect, giving a second tab. #>
    param([Parameter(Mandatory)] $Session, [int] $ConnectWaitSec = 8)
    Send-NutshellCommand -Session $Session -Id $script:IDM_FILE_CONNECT -SettleMs 1200
    $sm = (Get-NutshellWindows -Session $Session) | Where-Object { $_ -match "Session Manager" } | Select-Object -First 1
    if (-not $sm) { throw "Session Manager did not open" }
    $hsm = [IntPtr][long]($sm -split "`t")[0]
    $list = [NutshellNative]::GetDlgItem($hsm, 1000)                                  # IDC_LIST_SESSIONS
    [NutshellNative]::SendMsg($list, 0x0186, [IntPtr]0, [IntPtr]::Zero) | Out-Null     # LB_SETCURSEL 0
    [NutshellNative]::PostMessage($hsm, $script:WM_COMMAND, [IntPtr]((1 -shl 16) -bor 1000), $list) | Out-Null  # LBN_SELCHANGE
    Start-Sleep -Milliseconds 400
    [NutshellNative]::PostMessage($hsm, $script:WM_COMMAND, [IntPtr]1, [IntPtr]::Zero) | Out-Null   # IDOK = Connect
    Start-Sleep -Seconds $ConnectWaitSec
}

function Select-NutshellTab {
    <# Click tab N (0-based) in the tab strip, then refocus the terminal. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [int] $Index)
    # The tab strip is the top SZ_TAB_H (32 px at 96 DPI) of the client area;
    # tabs start after the "+" button. Client coordinates sidestep the title
    # and menu bars, whose heights vary with DPI.
    $scale = [NutshellNative]::GetDpiForWindow($Session.Main) / 96.0
    $x = [int]((100 + 160 * $Index) * $scale)
    $y = [int](16 * $scale)
    [NutshellNative]::ClickClient($Session.Main, $x, $y)
    Start-Sleep -Milliseconds 700
}

function Get-NutshellAiConfig {
    <#
    .SYNOPSIS
        AI settings for the AI cases, or $null when no key is available.
        Sources, in order: $env:NUTSHELL_IT_AI_KEY; tests/integration/.ai_key;
        tests/integration/.ai_config/nutshell.config (saved from a Nutshell
        instance run in that folder — the key stays encrypted with this
        machine's key material and is passed through as-is, and the provider
        and model saved there are used too). All three are git-ignored.
    #>
    if ($env:NUTSHELL_IT_AI_KEY) { return @{ Key = $env:NUTSHELL_IT_AI_KEY.Trim(); Provider = $null; Model = $null } }
    $f = Join-Path $PSScriptRoot ".ai_key"
    if (Test-Path $f) { $k = (Get-Content $f -Raw).Trim(); if ($k) { return @{ Key = $k; Provider = $null; Model = $null } } }
    $c = Join-Path $PSScriptRoot ".ai_config\nutshell.config"
    if (Test-Path $c) {
        try {
            $j = Get-Content $c -Raw | ConvertFrom-Json
            $k = [string]$j.settings.ai_api_key
            if ($k) { return @{ Key = $k; Provider = [string]$j.settings.ai_provider; Model = [string]$j.settings.ai_custom_model } }
        } catch { }
    }
    return $null
}

function Get-NutshellAiKey {
    <# Just the key from Get-NutshellAiConfig, or $null. #>
    $c = Get-NutshellAiConfig
    if ($c) { return $c.Key }
    return $null
}

function Get-NutshellAiPanel {
    <# HWND of the docked AI Assist panel (child of the main window), or Zero. #>
    param([Parameter(Mandatory)] $Session)
    return [NutshellNative]::FindWindowEx($Session.Main, [IntPtr]::Zero, "Nutshell_AIChat", [IntPtr]::Zero)
}

function Open-NutshellAiPanel {
    <# View > AI Assist, then return the docked panel HWND (throws if it did not appear). #>
    param([Parameter(Mandatory)] $Session)
    Send-NutshellCommand -Session $Session -Id 2020 -SettleMs 1500      # IDM_VIEW_AI_CHAT
    $p = Get-NutshellAiPanel -Session $Session
    if ($p -eq [IntPtr]::Zero) { throw "AI Assist panel did not open (no API key, or no session?)" }
    return $p
}

function Send-NutshellAiPrompt {
    <# Put text in the AI input box and press Send. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Text)
    $p = Get-NutshellAiPanel -Session $Session
    if ($p -eq [IntPtr]::Zero) { throw "AI Assist panel is not open" }
    $input = [NutshellNative]::GetDlgItem($p, 4002)                                   # IDC_CHAT_INPUT
    [NutshellNative]::SendMsgStr($input, 0x000C, [IntPtr]::Zero, $Text) | Out-Null   # WM_SETTEXT
    [NutshellNative]::PostMessage($p, $script:WM_COMMAND, [IntPtr]4003, [IntPtr]::Zero) | Out-Null  # IDC_CHAT_SEND
    Start-Sleep -Milliseconds 300
}

function Set-NutshellTerminalFocus {
    <# Click inside the terminal area so keystrokes go to the shell, not the AI input box. #>
    param([Parameter(Mandatory)] $Session)
    # Click low in the terminal's client area (near the prompt, left of any
    # docked AI panel), then bring the window forward so SendKeys targets it.
    $scale = [NutshellNative]::GetDpiForWindow($Session.Main) / 96.0
    $c = [NutshellNative]::ClientSize($Session.Main)
    [NutshellNative]::ClickClient($Session.Main, [int](100 * $scale), [int]($c.B - 60 * $scale))
    [NutshellNative]::SetForegroundWindow($Session.Main) | Out-Null
    Start-Sleep -Milliseconds 300
}

function Set-NutshellAiAutoApprove {
    <# Toggle the session's Auto Approve button (IDC_CHAT_AUTOAPPROVE). #>
    param([Parameter(Mandatory)] $Session)
    $p = Get-NutshellAiPanel -Session $Session
    if ($p -eq [IntPtr]::Zero) { throw "AI Assist panel is not open" }
    [NutshellNative]::PostMessage($p, $script:WM_COMMAND, [IntPtr]4015, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 300
}

function Wait-NutshellAiSendIdle {
    <# Poll the AI panel's Send button (IDC_CHAT_SEND, 4003) until its text
       is back to ">" -- busy (streaming a reply) or dispatching (running
       approved commands) both show the stop glyph instead. Used to know a
       reply has actually finished before acting on it, per docs/superpowers/
       specs/2026-09-09-pending-command-batches.md's integration case. #>
    param([Parameter(Mandatory)] $Session, [int] $TimeoutSec = 90)
    $p = Get-NutshellAiPanel -Session $Session
    if ($p -eq [IntPtr]::Zero) { throw "AI Assist panel is not open" }
    $btn = [NutshellNative]::GetDlgItem($p, 4003)                                    # IDC_CHAT_SEND
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $sb = New-Object System.Text.StringBuilder 16
    while ((Get-Date) -lt $deadline) {
        [void]$sb.Clear()
        [NutshellNative]::GetWindowText($btn, $sb, $sb.Capacity) | Out-Null
        if ($sb.ToString() -eq ">") { return }
        Start-Sleep -Milliseconds 500
    }
    throw "Send button never returned to idle (`">`") within ${TimeoutSec}s"
}

function Wait-NutshellShell {
    <# Press Enter (posted -- no foreground needed) until a shell prompt ($ or
       #) shows up in the log. Throws on timeout. 60 s because the login shell
       on a small host can spend 20-30 s in its MOTD scripts (update checks)
       before the first prompt, which showed up as a flake at 25 s. #>
    param([Parameter(Mandatory)] $Session, [int] $TimeoutSec = 60)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Send-NutshellKey -Session $Session -Key Enter -SettleMs 700
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

# ---- Dialog helpers -------------------------------------------------------------
# Every helper below finds a control by its real Win32 control id (from
# src/ui/resource.h or the #define block at the top of the .c file that owns
# it) and drives it with the same message a real click/selection sends, posted
# or sent directly to the control -- no foreground, no focus, no unlocked
# desktop needed. See $script:DIALOG_CLASSES above for which window class each
# of Nutshell's own dialogs uses.

function Wait-NutshellDialog {
    <# HWND of a top-level window owned by the session process whose class is
       one of Nutshell's known dialog classes (see $script:DIALOG_CLASSES) and,
       if -Title is given, whose title matches that regex. Zero if none
       appears within -TimeoutSec (never throws -- callers that need "must
       appear" semantics should Assert-True on the return value, matching the
       existing $dlg patterns in Run-Integration.ps1's AI cases). #>
    param([Parameter(Mandatory)] $Session, [string] $Title = $null, [int] $TimeoutSec = 10)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        foreach ($w in (Get-NutshellWindows -Session $Session)) {
            $parts = $w -split "`t"
            $cls = $parts[1]; $txt = $parts[2]
            if ($script:DIALOG_CLASSES -contains $cls) {
                if (-not $Title -or $txt -match $Title) { return [IntPtr][long]$parts[0] }
            }
        }
        Start-Sleep -Milliseconds 200
    }
    return [IntPtr]::Zero
}

function Get-NutshellControl {
    <# Child HWND of -Dialog by control id: GetDlgItem first (covers a true
       dialog template and any direct-child custom window), then a recursive
       EnumChildWindows/GetDlgCtrlID fallback for a control nested inside a
       sub-page (e.g. a Settings page, which is itself a child window of the
       Settings window -- GetDlgItem only searches direct children). Zero if
       not found. #>
    param([Parameter(Mandatory)] $Dialog, [Parameter(Mandatory)] [int] $Id)
    $h = [NutshellNative]::GetDlgItem($Dialog, $Id)
    if ($h -ne [IntPtr]::Zero) { return $h }
    return [NutshellNative]::FindChildById($Dialog, $Id)
}

function Set-NutshellControlText {
    <# WM_SETTEXT, then an EN_CHANGE notification to the parent. No dialog in
       this codebase actually reads EN_CHANGE for validation (settings.c's
       IDOK handler reads every field's live text/state directly via
       GetDlgItemText/IsDlgButtonChecked/CB_GETCURSEL at Save time, and no
       other dialog wires EN_CHANGE either -- confirmed by grepping src/ui for
       EN_CHANGE), so this is a no-op in practice today; sent anyway so a
       future control that does listen for it keeps working without a harness
       change. #>
    param([Parameter(Mandatory)] $Control, [Parameter(Mandatory)] [string] $Text)
    [NutshellNative]::SendMsgStr($Control, $script:WM_SETTEXT, [IntPtr]::Zero, $Text) | Out-Null
    $parent = [NutshellNative]::GetParent($Control)
    $id = [NutshellNative]::GetDlgCtrlID($Control)
    if ($parent -ne [IntPtr]::Zero) {
        [NutshellNative]::PostMessage($parent, $script:WM_COMMAND, [IntPtr]((($script:EN_CHANGE) -shl 16) -bor ($id -band 0xFFFF)), $Control) | Out-Null
    }
    Start-Sleep -Milliseconds 100
}

function Get-NutshellControlText {
    param([Parameter(Mandatory)] $Control)
    $sb = New-Object System.Text.StringBuilder 1024
    [NutshellNative]::GetWindowText($Control, $sb, $sb.Capacity) | Out-Null
    return $sb.ToString()
}

function Invoke-NutshellButton {
    <# BM_CLICK via SendMessage: the standard way to simulate a real click on
       any BUTTON-class control (works the same whether it's owner-drawn --
       every push button in this codebase is, via draw_themed_button/
       WM_DRAWITEM -- since owner-draw only changes painting, not the button
       window procedure's own message handling). Tested against Session
       Manager's Save/Connect/Cancel/New/Edit/Delete, Settings' Save/Cancel,
       the paste preview's Paste/Cancel and the passphrase prompt's OK/Cancel
       -- all plain BUTTON children created with IDOK/IDCANCEL/IDC_* ids, so
       BM_CLICK was sufficient everywhere; WM_COMMAND/BN_CLICKED to the parent
       was never needed as a fallback. #>
    param([Parameter(Mandatory)] $Control)
    [NutshellNative]::SendMsg($Control, $script:BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 300
}

function Select-NutshellCombo {
    <# -Item is an index (int) or exact display text (string, via
       CB_FINDSTRINGEXACT). Posts CBN_SELCHANGE to the parent afterwards. #>
    param([Parameter(Mandatory)] $Control, [Parameter(Mandatory)] $Item)
    $parent = [NutshellNative]::GetParent($Control)
    $id = [NutshellNative]::GetDlgCtrlID($Control)
    if ($Item -is [int]) {
        [NutshellNative]::SendMsg($Control, $script:CB_SETCURSEL, [IntPtr]$Item, [IntPtr]::Zero) | Out-Null
    } else {
        $idx = [NutshellNative]::SendMsgStr($Control, $script:CB_FINDSTRINGEXACT, [IntPtr](-1), [string]$Item)
        if ($idx.ToInt32() -lt 0) { throw "combo item '$Item' not found" }
        [NutshellNative]::SendMsg($Control, $script:CB_SETCURSEL, $idx, [IntPtr]::Zero) | Out-Null
    }
    if ($parent -ne [IntPtr]::Zero) {
        [NutshellNative]::PostMessage($parent, $script:WM_COMMAND, [IntPtr]((($script:CBN_SELCHANGE) -shl 16) -bor ($id -band 0xFFFF)), $Control) | Out-Null
    }
    Start-Sleep -Milliseconds 200
}

function Get-NutshellComboSelection {
    <# 0-based selected index, or -1 if nothing is selected. Pair with
       Get-NutshellControlText for the selected item's display text (WM_GETTEXT
       on a combo box returns its edit portion, which for CBS_DROPDOWNLIST is
       the selected item's text). #>
    param([Parameter(Mandatory)] $Control)
    return [NutshellNative]::SendMsg($Control, $script:CB_GETCURSEL, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
}

function Set-NutshellCheckbox {
    param([Parameter(Mandatory)] $Control, [Parameter(Mandatory)] [bool] $Checked)
    $val = if ($Checked) { 1 } else { 0 }
    [NutshellNative]::SendMsg($Control, $script:BM_SETCHECK, [IntPtr]$val, [IntPtr]::Zero) | Out-Null
    $parent = [NutshellNative]::GetParent($Control)
    $id = [NutshellNative]::GetDlgCtrlID($Control)
    if ($parent -ne [IntPtr]::Zero) {
        [NutshellNative]::PostMessage($parent, $script:WM_COMMAND, [IntPtr](0 -bor ($id -band 0xFFFF)), $Control) | Out-Null  # BN_CLICKED (=0)
    }
    Start-Sleep -Milliseconds 150
}

function Get-NutshellCheckbox {
    param([Parameter(Mandatory)] $Control)
    return ([NutshellNative]::SendMsg($Control, $script:BM_GETCHECK, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -eq $script:BST_CHECKED)
}

function Select-NutshellListItem {
    param([Parameter(Mandatory)] $Control, [Parameter(Mandatory)] [int] $Index)
    [NutshellNative]::SendMsg($Control, $script:LB_SETCURSEL, [IntPtr]$Index, [IntPtr]::Zero) | Out-Null
    $parent = [NutshellNative]::GetParent($Control)
    $id = [NutshellNative]::GetDlgCtrlID($Control)
    if ($parent -ne [IntPtr]::Zero) {
        [NutshellNative]::PostMessage($parent, $script:WM_COMMAND, [IntPtr]((($script:LBN_SELCHANGE) -shl 16) -bor ($id -band 0xFFFF)), $Control) | Out-Null
    }
    Start-Sleep -Milliseconds 200
}

function Get-NutshellListItems {
    param([Parameter(Mandatory)] $Control)
    $count = [NutshellNative]::SendMsg($Control, $script:LB_GETCOUNT, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    $items = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $count; $i++) {
        $len = [NutshellNative]::SendMsg($Control, $script:LB_GETTEXTLEN, [IntPtr]$i, [IntPtr]::Zero).ToInt32()
        $sb = New-Object System.Text.StringBuilder ($len + 1)
        [NutshellNative]::SendMsgSb($Control, $script:LB_GETTEXT, [IntPtr]$i, $sb) | Out-Null
        [void]$items.Add($sb.ToString())
    }
    return $items
}

function Close-NutshellDialog {
    <# Click OK/Cancel/Yes/No. IDOK=1/IDCANCEL=2 are the real control ids on
       every Nutshell dialog's own OK/Cancel-equivalent button (Session
       Manager's Connect is IDOK, Cancel is IDCANCEL; Settings' Save is IDOK,
       Cancel is IDCANCEL; the paste preview's Paste is IDOK, Cancel is
       IDCANCEL; the passphrase prompt's OK/Cancel are IDOK/IDCANCEL).
       IDYES=6/IDNO=7 are the standard ids MessageBox(MB_YESNO) assigns its
       buttons (the host-key TOFU/mismatch dialogs), so no title/caption
       lookup is needed there either. Falls back to a Button child matched by
       its caption text (documented in the brief as "FindWindowEx with class
       Button and caption") only if the standard id isn't present -- not
       exercised by any dialog observed in this codebase, since all of them
       use the standard ids above. #>
    param([Parameter(Mandatory)] $Dialog, [ValidateSet("OK", "Cancel", "Yes", "No")] [string] $Button = "OK")
    $idMap = @{ OK = 1; Cancel = 2; Yes = 6; No = 7 }
    $ctrl = Get-NutshellControl -Dialog $Dialog -Id $idMap[$Button]
    if ($ctrl -eq [IntPtr]::Zero) {
        $match = [NutshellNative]::ListChildren($Dialog) | Where-Object {
            $p = $_ -split "`t"; $p[2] -eq "Button" -and $p[3] -eq $Button
        } | Select-Object -First 1
        if (-not $match) { throw "no '$Button' control found on dialog $Dialog" }
        $ctrl = [IntPtr][long]($match -split "`t")[0]
    }
    Invoke-NutshellButton -Control $ctrl
}

function Open-NutshellSessionManager {
    <# File > New Session (IDM_FILE_NEW_SESSION), then wait for the Session
       Manager dialog (class #32770, caption "Session Manager" -- see
       IDD_SESSION_MANAGER in resource.rc). Throws if it doesn't appear. #>
    param([Parameter(Mandatory)] $Session, [int] $TimeoutSec = 5)
    Send-NutshellCommand -Session $Session -Id $script:IDM_FILE_NEW_SESSION -SettleMs 800
    $dlg = Wait-NutshellDialog -Session $Session -Title "Session Manager" -TimeoutSec $TimeoutSec
    if ($dlg -eq [IntPtr]::Zero) { throw "Session Manager did not open" }
    return $dlg
}

function Select-NutshellSettingsPage {
    <# Select a page in the Settings nav listbox (IDC_SETTINGS_NAV) by its
       exact display name -- one of the nine selectable entries in
       src/core/settings_layout.c's NAV_TABLE: Appearance, Terminal, Logging,
       SSH, Startup, Provider, Behaviour, "Web Access", About (the other three
       NAV_TABLE rows are non-selectable group headers: General, Connection,
       AI Assistant). #>
    param([Parameter(Mandatory)] $Dialog, [Parameter(Mandatory)] [string] $Page)
    $nav = Get-NutshellControl -Dialog $Dialog -Id $script:IDC_SETTINGS_NAV
    if ($nav -eq [IntPtr]::Zero) { throw "settings nav list control not found" }
    $count = [NutshellNative]::SendMsg($nav, $script:LB_GETCOUNT, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    $found = -1
    for ($i = 0; $i -lt $count; $i++) {
        $len = [NutshellNative]::SendMsg($nav, $script:LB_GETTEXTLEN, [IntPtr]$i, [IntPtr]::Zero).ToInt32()
        $sb = New-Object System.Text.StringBuilder ($len + 1)
        [NutshellNative]::SendMsgSb($nav, $script:LB_GETTEXT, [IntPtr]$i, $sb) | Out-Null
        if ($sb.ToString() -eq $Page) { $found = $i; break }
    }
    if ($found -lt 0) { throw "settings page '$Page' not found in nav list" }
    Select-NutshellListItem -Control $nav -Index $found
}

function Open-NutshellSettings {
    <# Edit > Settings (IDM_EDIT_SETTINGS), then wait for the Settings window
       (class Nutshell_Settings, caption "Settings"). -Page, if given, selects
       that nav entry (see Select-NutshellSettingsPage) before returning. #>
    param([Parameter(Mandatory)] $Session, [string] $Page = $null, [int] $TimeoutSec = 5)
    Send-NutshellCommand -Session $Session -Id $script:IDM_EDIT_SETTINGS -SettleMs 800
    $dlg = Wait-NutshellDialog -Session $Session -Title "Settings" -TimeoutSec $TimeoutSec
    if ($dlg -eq [IntPtr]::Zero) { throw "Settings did not open" }
    if ($Page) { Select-NutshellSettingsPage -Dialog $dlg -Page $Page }
    return $dlg
}

function Get-NutshellTabCount {
    <# Not reliably implementable: tabs.c's tab strip is a single owner-drawn
       Nutshell_Tabs window (WM_PAINT-only, no child HWND per tab, no
       WM_GETOBJECT/UI-Automation provider, no exposed count message) --
       there is no message-based or UIA way to ask it how many tabs exist
       without clicking around and comparing captures. Documented product gap;
       throws rather than returning a guessed/pixel-scraped number. #>
    param([Parameter(Mandatory)] $Session)
    throw "Get-NutshellTabCount: tabs.c exposes no message-based or UIA tab count (single owner-drawn HWND, no per-tab child windows) -- not implementable without clicking/pixel-scraping; see NutshellIT.psm1 for detail"
}

function Close-NutshellTab {
    <# Click tab -Index active (Select-NutshellTab -- the tab strip only
       responds to real clicks, per tabs.c's WM_LBUTTONDOWN-only hit testing,
       so this still needs the foreground/mouse and does not work
       desktop-locked), then post Ctrl+W's WM_CHAR (0x17) to close the now-
       active tab -- window.c's WM_CHAR handler special-cases 0x17
       (on_tab_close on the active tab) independent of actual keyboard focus,
       so that half of the operation IS posted. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [int] $Index)
    Select-NutshellTab -Session $Session -Index $Index
    [NutshellNative]::PostMessage($Session.Main, $script:WM_CHAR, [IntPtr]0x17, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 400
}

function Get-NutshellWindowText {
    param([Parameter(Mandatory)] $Hwnd)
    $sb = New-Object System.Text.StringBuilder 1024
    [NutshellNative]::GetWindowText([IntPtr]$Hwnd, $sb, $sb.Capacity) | Out-Null
    return $sb.ToString()
}

function Get-NutshellChildWindows {
    <# All descendant windows of -Hwnd (any depth) as "hwnd`tctrlId`tclass`ttext"
       strings, for debugging a case interactively. #>
    param([Parameter(Mandatory)] $Hwnd)
    return [NutshellNative]::ListChildren([IntPtr]$Hwnd)
}

# ---- Screen oracle ----------------------------------------------------------------

function Get-NutshellPixel {
    param([Parameter(Mandatory)] [string] $Path, [Parameter(Mandatory)] [int] $X, [Parameter(Mandatory)] [int] $Y)
    $bmp = New-Object System.Drawing.Bitmap $Path
    try {
        $c = $bmp.GetPixel($X, $Y)
        return [pscustomobject]@{ R = [int]$c.R; G = [int]$c.G; B = [int]$c.B }
    } finally { $bmp.Dispose() }
}

function Test-NutshellPixelNear {
    <# True if the pixel at (X, Y) in the PNG at -Path is within -Tolerance
       (per channel) of -Rgb (an @(r, g, b) array). #>
    param([Parameter(Mandatory)] [string] $Path, [Parameter(Mandatory)] [int] $X, [Parameter(Mandatory)] [int] $Y,
          [Parameter(Mandatory)] [int[]] $Rgb, [int] $Tolerance = 12)
    $p = Get-NutshellPixel -Path $Path -X $X -Y $Y
    $dr = [Math]::Abs($p.R - $Rgb[0]); $dg = [Math]::Abs($p.G - $Rgb[1]); $db = [Math]::Abs($p.B - $Rgb[2])
    return ($dr -le $Tolerance -and $dg -le $Tolerance -and $db -le $Tolerance)
}

# Flat 0xRRGGBB fields of ThemeTokens (src/core/ui_theme.h) that
# Get-NutshellThemeColor can read out of src/core/ui_theme.c's per-theme C
# initialisers. The "chat block" array (user_bubble, cmd_bg, indicator_*, ...)
# is not a named field -- it's a positional array literal -- so it is not
# supported here; add a token here (and confirm its field name in ui_theme.c)
# before relying on it.
$script:THEME_TOKENS = @("bg_primary", "bg_secondary", "accent", "text_main", "text_dim",
                          "border", "terminal_fg", "terminal_bg", "success", "warning", "danger", "info", "link")

function Get-NutshellThemeColor {
    <# Parse src/core/ui_theme.c's C initialisers for theme -Name (e.g. "Onyx
       Light") and return -Token (see $script:THEME_TOKENS) as an
       @{R;G;B} pscustomobject. Regex-based on the ".field = 0xRRGGBB,"
       literal syntax those structs are written in -- if ui_theme.c's style
       changes (e.g. to a helper macro) this needs updating alongside it. #>
    param([Parameter(Mandatory)] [string] $Name, [Parameter(Mandatory)] [string] $Token)
    if ($script:THEME_TOKENS -notcontains $Token) {
        throw "unsupported theme token '$Token' (supported: $($script:THEME_TOKENS -join ', '))"
    }
    $path = Join-Path $PSScriptRoot "..\..\src\core\ui_theme.c"
    if (-not (Test-Path $path)) { throw "ui_theme.c not found at $path" }
    $text = Get-Content $path -Raw
    $blocks = [regex]::Matches($text, '\.name\s*=\s*"([^"]+)"(.*?)(?=\.name\s*=\s*"|\z)',
        [System.Text.RegularExpressions.RegexOptions]::Singleline)
    foreach ($m in $blocks) {
        if ($m.Groups[1].Value -ne $Name) { continue }
        $tm = [regex]::Match($m.Groups[2].Value, '\.' + [regex]::Escape($Token) + '\s*=\s*0x([0-9A-Fa-f]{6})')
        if (-not $tm.Success) { throw "token '$Token' not found in theme '$Name' in ui_theme.c" }
        $hex = $tm.Groups[1].Value
        return [pscustomobject]@{
            R = [Convert]::ToInt32($hex.Substring(0, 2), 16)
            G = [Convert]::ToInt32($hex.Substring(2, 2), 16)
            B = [Convert]::ToInt32($hex.Substring(4, 2), 16)
        }
    }
    throw "theme '$Name' not found in ui_theme.c"
}

function Get-NutshellExeVersion {
    <# nutshell.exe -v (cli_args.c) calls AttachConsole(ATTACH_PARENT_PROCESS)
       then freopen("CONOUT$", "w", stdout) (src/main.c's cli_output) -- it
       writes straight to the console device, bypassing any redirected stdout
       handle, so a normal `> file`/-RedirectStandardOutput capture reads back
       empty. [NutshellNative]::ReadConsoleTail (above) can recover that text
       by opening "CONOUT$" itself and reading the screen buffer back -- this
       was verified working standalone (run un-redirected with -NoNewWindow so
       the child shares this process's console, then ReadConsoleTail after it
       exits) -- BUT Start-Process -NoNewWindow hung indefinitely (10+ minutes,
       never returned control even past a WaitForExit(5000) timeout guard) when
       invoked from inside this harness's actual nested automation shell
       (itself launched by a PowerShell tool with no ordinary interactive
       console), which matches a known class of Start-Process -NoNewWindow /
       headless-console hang. Since that would block the whole suite -- the
       opposite of what a desktop-lock-safe harness is for -- -Exe validation
       does NOT spawn the exe at all: it reads the FileVersion straight out of
       the binary's embedded VERSIONINFO resource (nutshell.rc's
       FILEVERSION/PRODUCTVERSION, populated from resource.h's
       APP_VERSION_BINARY), which is exactly the same number and always safe
       (no process, no console, cannot hang). #>
    param([Parameter(Mandatory)] [string] $Exe)
    if (-not (Test-Path $Exe)) { return $null }
    try {
        $info = [System.Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path $Exe).Path)
        if ($info.FileVersion) { return $info.FileVersion.Trim() }
    } catch { }
    return $null
}

Export-ModuleMember -Function New-NutshellTestEnv, Start-Nutshell, Stop-Nutshell, Get-NutshellWindows, `
    Send-NutshellCommand, Start-NutshellLogging, Send-NutshellKeys, Send-NutshellText, Send-NutshellKey, Send-NutshellLine, `
    Get-NutshellLogText, Wait-NutshellLog, Wait-NutshellShell, Set-NutshellWindowSize, Save-NutshellScreenshot, `
    Open-NutshellSecondTab, Select-NutshellTab, `
    Get-NutshellAiConfig, Get-NutshellAiKey, Get-NutshellAiPanel, Open-NutshellAiPanel, Send-NutshellAiPrompt, Set-NutshellAiAutoApprove, Set-NutshellTerminalFocus, `
    Wait-NutshellAiSendIdle, `
    Wait-NutshellDialog, Get-NutshellControl, Set-NutshellControlText, Get-NutshellControlText, Invoke-NutshellButton, `
    Select-NutshellCombo, Get-NutshellComboSelection, Set-NutshellCheckbox, Get-NutshellCheckbox, `
    Select-NutshellListItem, Get-NutshellListItems, Close-NutshellDialog, `
    Open-NutshellSessionManager, Open-NutshellSettings, Select-NutshellSettingsPage, `
    Get-NutshellTabCount, Close-NutshellTab, Get-NutshellWindowText, Get-NutshellChildWindows, `
    Get-NutshellPixel, Test-NutshellPixelNear, Get-NutshellThemeColor, Get-NutshellExeVersion
