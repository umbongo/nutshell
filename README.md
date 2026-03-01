# Conga.SSH

A lightweight SSH terminal client for Windows 11, built with Go and Fyne. No admin privileges required.

## Features

- **Multi-tab SSH sessions** — connect to multiple servers simultaneously
- **VT100 terminal emulation** with colour support and 3000-line scrollback buffer
- **Password and SSH key authentication** — password is the default, key auth available per profile
- **Encrypted credential storage** — AES-GCM encryption with machine-derived keys
- **Session logging** — toggle on/off via right-click tab menu; green "L" badge shows live logging state
- **Tab status indicators** — colour-coded connection dot and hover tooltip (user, host, log path, connection duration)
- **Smart paste** — confirmation dialog with optional per-line delay (default 350ms)
- **Customisable appearance** — font, font size, foreground/background colours with presets and colour picker
- **Portable** — all configuration stored in `settings.ini` next to the executable

## Requirements

- Windows 11
- Executable must be placed in a writeable directory (no admin required)

## Configuration

All settings are stored in `settings.ini`, created automatically on first launch with defaults. Configurable options include:

| Setting | Default |
|---|---|
| Font | Consolas |
| Font size | 12 |
| Scrollback buffer | 3000 lines |
| Paste delay per line | 350ms |
| Foreground colour | #CCCCCC (white) |
| Background colour | #0C0C0C (black) |
| Host key verification | Off |
| Session logging | Off |
| Log file format | `{date}_{time}_{host}.log` |

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| Ctrl + Mouse Wheel | Increase/decrease font size |
| Ctrl + Up/Down | Increase/decrease font size |

## Building

### Windows (native)

Requires [MSYS2](https://www.msys2.org/) with the MinGW-w64 toolchain. Install once:

```bash
winget install MSYS2.MSYS2
C:/msys64/usr/bin/pacman.exe -S --noconfirm mingw-w64-x86_64-gcc
```

Then build:

```bash
PATH="/c/msys64/mingw64/bin:$PATH" CGO_ENABLED=1 go build -ldflags "-H windowsgui" -o conga-ssh.exe .
```

To avoid setting `PATH` every time, add `C:\msys64\mingw64\bin` to your system `PATH` permanently via System Properties → Environment Variables, then just run:

```bash
CGO_ENABLED=1 go build -ldflags "-H windowsgui" -o conga-ssh.exe .
```

Note: first build takes several minutes as it compiles OpenGL/GLFW.

### Windows executable (cross-compile from Linux)

Requires `mingw-w64`:

```bash
sudo apt-get install gcc-mingw-w64-x86-64
CGO_ENABLED=1 CC=x86_64-w64-mingw32-gcc GOOS=windows GOARCH=amd64 go build -ldflags "-H windowsgui" -o conga-ssh.exe .
```

Note: first build takes several minutes as it compiles OpenGL/GLFW through mingw.

### Linux (for development and testing)

Requires X11 dev headers:

```bash
sudo apt-get install libxcursor-dev libxi-dev libxinerama-dev libxrandr-dev
go build -o conga-ssh .
```

## Running Tests

```bash
go test ./internal/...
```

## Project Structure

```
conga.ssh/
├── main.go                      # Entry point
├── settings.ini                 # Auto-created on first launch
├── internal/
│   ├── config/                  # Settings and profile management
│   ├── crypto/                  # AES-GCM encryption, machine key derivation
│   ├── ssh/                     # SSH session and PTY management
│   └── ui/                      # Fyne GUI (app, tabs, connect dialog)
```

## Development Status

| Phase | Status | Scope |
|---|---|---|
| Phase 1 | ✅ Complete | SSH connection, Fyne GUI shell, VT100 terminal, password auth |
| Phase 2 | ✅ Complete | Session/profile management, encrypted storage, SSH key auth, TOFU host key verification |
| Phase 3 | ✅ Complete | Logging, paste enhancements, font/colour/scrollback settings |
| Phase 4 | ✅ Complete | Tab UI overhaul: L badge, connection dot, hover tooltip, 80% tab font, settings footer |

See [PRD.md](PRD.md) for full requirements.

## License

TBD
