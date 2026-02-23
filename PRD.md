# Product Requirements Document (PRD) for Conga.SSH

## Overview
Conga.SSH is a Golang-based SSH terminal client designed to run on Windows 11 without requiring administrator privileges. The application provides a tabbed graphical user interface for connecting to multiple SSH servers simultaneously, executing commands, and managing sessions. Settings and connection profiles are stored in an INI file located in the same directory as the executable. The user is responsible for placing the executable in a writeable directory.

## Objectives
- Provide a user-friendly SSH client for Windows 11 users.
- Ensure no administrative privileges are required for installation or operation.
- Securely store sensitive information like passwords using strong encryption.
- Offer essential SSH features with additional conveniences like session logging and advanced paste functionality.

## Target Audience
- Windows 11 users who need to connect to SSH servers for development, administration, or remote access.
- Users who prefer a GUI over command-line SSH tools.

## Features and Requirements

### Core Functionality
- **SSH Connection**: Ability to connect to SSH servers using hostname/IP, port, username, and password (default) or SSH key.
- **Interactive Terminal**: VT100 terminal emulation with colour support for executing commands on the remote server.
- **Session Management**: Save and load multiple connection profiles, including hostname, username, and encrypted password or key file path.
- **Multiple Sessions**: Support multiple concurrent SSH sessions, each in its own tab. Users can switch between active sessions using tabs.

### User Interface
- **GUI Framework**: Built with Fyne, a pure Go cross-platform GUI framework. This ensures clean cross-compilation from Linux to Windows with no CGo or native library dependencies.
- **Minimal Layout**: Clean, minimalistic design with a tabbed interface for managing multiple sessions.
- **Resizable Window**: The application window is fully resizable. Terminal resize signals are sent to the remote server when the window or terminal area is resized.
- **Font**:
  - Default font: Consolas (the standard Windows command prompt monospace font).
  - Font is configurable via the settings.
  - Font size adjustable with Ctrl+Mouse Wheel or Ctrl+Up/Down arrows.
- **Terminal Colours**:
  - Foreground and background colours are configurable in settings.
  - Defaults match the Windows command prompt: white text (#CCCCCC) on black background (#0C0C0C).
  - 10 common preset options for each (foreground and background):
    1. White (#CCCCCC)
    2. Black (#0C0C0C)
    3. Green (#13A10E)
    4. Bright Green (#16C60C)
    5. Blue (#0037DA)
    6. Cyan (#3A96DD)
    7. Red (#C50F1F)
    8. Yellow (#C19C00)
    9. Magenta (#881798)
    10. Grey (#767676)
  - Full colour palette picker available for both foreground and background for custom colour selection.
- **Terminal Emulation**: Integrated terminal window with:
  - VT100 terminal type.
  - Full colour support.
  - Mouse support for text selection and copy to clipboard.
  - Scrollback buffer defaulting to 3000 lines, configurable via settings.

### Authentication
- **Password Authentication (Default)**: Support for password-based authentication. This is the default and primary method.
- **SSH Key Authentication**: Support for SSH key-based authentication (public/private key pairs). Users can specify the path to a private key file in their connection profile.
- **Encrypted Storage**: Passwords must be stored in a strongly encrypted format (AES) in the INI file. Encryption keys are derived from machine-specific data (e.g., Windows machine GUID or similar hardware-bound identifier). Key file paths are stored as-is (not encrypted).

### Connection Error Handling
- **Connection Failure**: On connection failure (timeout, auth failure, unreachable host), display a clear error message describing the failure reason.
- **Reconnect/Close**: Provide two buttons on the error screen:
  - **Reconnect**: Retry the connection with the same profile settings.
  - **Close**: Close the session tab.
- **Dropped Connections**: If an active session is disconnected unexpectedly, display an error in the terminal area with the same Reconnect/Close options.

### Advanced Features
- **Copy and Paste**:
  - Standard copy/paste functionality.
  - Mouse selection in the terminal copies to clipboard.
  - Confirmation dialog before executing a paste operation.
  - Option to add a delay per line when pasting multiple lines to prevent command flooding.
- **Session Logging**:
  - Enable/disable logging with a single click.
  - Log file name format is configurable in settings using placeholders: `{date}`, `{time}`, `{host}`.
  - Default format: `{date}_{time}_{host}.log` (e.g., `2026-02-24_4.32pm_localhost.log`).
  - Logs capture session activity for review.

### Configuration
- **INI File Storage**: All settings and profiles stored in an INI file (`settings.ini`) in the same directory as the executable. The file is automatically created with default values when the application is first started. The user must ensure the executable is placed in a writeable directory.
- **No Admin Required**: Ensure the application and its storage do not require elevated privileges.
- **Configurable Settings** (stored in INI):
  - Default font and font size.
  - Scrollback buffer size (default: 3000 lines).
  - Paste delay per line (default: 350ms).
  - Session logging on/off default.
  - Log file name format (using `{date}`, `{time}`, `{host}` placeholders).
  - Log file output directory.
  - Host key verification on/off (default: off).
  - Terminal foreground colour (default: #CCCCCC).
  - Terminal background colour (default: #0C0C0C).

### Security
- **Encryption**: AES encryption for storing passwords in the INI file. Encryption key derived from machine-specific data (Windows machine GUID or equivalent hardware-bound value).
- **Host Key Verification**: Configurable in settings. Default is disabled. When enabled, uses trust-on-first-use (TOFU) model: accepts and stores the host key on first connection, warns if it changes on subsequent connections.
- **Data Handling**: Securely handle sensitive data in memory and storage.

### Platform Requirements
- **Operating System**: Windows 11.
- **Dependencies**: Minimal external dependencies; pure Golang with Fyne for GUI.
- **Build**: Cross-compile from Linux to Windows executable.

### Testing
- **Iterative Testing**: Unit and integration tests are written alongside each feature, not deferred to a later phase.
- **Unit Tests**: Cover individual modules (encryption, INI parsing, profile management, terminal emulation logic, log file naming).
- **Integration Tests**: Cover end-to-end flows (SSH connection lifecycle, session tab management, settings persistence, reconnect behaviour).
- **Each phase** must include passing tests for its deliverables before moving to the next phase.

### Non-Functional Requirements
- **Performance**: Responsive GUI and terminal emulation.
- **Usability**: Intuitive interface for saving/loading profiles and managing sessions.
- **Maintainability**: Clean, modular Golang code.
- **Compatibility**: Ensure compatibility with common SSH servers.

## Assumptions
- Users have basic knowledge of SSH concepts.
- The application will use standard SSH protocol (port 22 by default).
- Encryption key for passwords is derived from machine-specific data (profiles are not portable between machines).
- The executable is placed in a writeable directory by the user.

## Risks and Mitigations
- **Security Risks**: Implement AES encryption for passwords. Machine-derived keys mean profiles are not portable, but this is an acceptable trade-off for convenience (no master password required).
- **Cross-Platform Build**: Test cross-compilation thoroughly, especially Fyne's Windows rendering.
- **Terminal Emulation Reliability**: Use a proven Go terminal emulation library (e.g., github.com/creack/pty or similar VT100 parser) rather than building from scratch.

## Future Enhancements
- File transfer via SCP/SFTP.
- Port forwarding/tunneling.
- SSH agent forwarding.

## Timeline
- **Phase 1**: Basic SSH connection, Fyne GUI shell with tabbed layout, VT100 terminal emulation, password authentication.
- **Phase 2**: Session/profile management, AES password encryption with machine-derived keys, SSH key authentication support.
- **Phase 3**: Advanced features (session logging, paste confirmation with per-line delay, font configuration, scrollback settings).
- **Phase 4**: Final integration testing, polish, and release.

## Stakeholders
- Developer: Thomas
- Users: Windows 11 SSH users

## Approval
Pending review and approval.