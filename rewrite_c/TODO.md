# To-Do List

All planned phases are **complete**. 258 tests pass, build clean, lint clean.

---

## Completed Phases

| Phase | Summary |
|-------|---------|
| 1. Foundation | Makefile (cross-compile + test + lint + debug), test framework, xmalloc, vector, string_utils, logger |
| 2. Config | JSON tokenizer, JSON parser (recursive descent), config loader/saver, `config.json` profiles + settings |
| 3. Terminal | VT100/ANSI parser, screen buffer with scrollback ring, cursor movement, SGR attributes |
| 4. SSH | libssh2 + OpenSSL linkage, session/channel/PTY, non-blocking I/O loop |
| 5. UI (Win32) | Main window, GDI renderer, keyboard input, resize handling, owner-drawn tab strip |
| 6. Polish | Application icon and version resource |
| 7. Password Encryption (P1-01) | AES-256-GCM via OpenSSL, PBKDF2-SHA256 key from MachineGuid; auto-encrypt on save, decrypt on load |
| 8. TOFU Host Keys (P1-02) | `knownhosts.c` + libssh2 knownhost API; first-connect dialog, mismatch warning |
| 9. PTY Resize (P1-03) | `ssh_pty_resize()` with last_cols/last_rows dedup; wired in WM_SIZE and apply_zoom() |
| 10. Key Passphrase (P1-04) | Win32 passphrase dialog, retry auth, cache in session (zeroed on free) |
| 11. 256-Color/Truecolor (P2-02) | Extended SGR parser (38;5;n, 48;5;n, 38;2;r;g;b), 6×6×6 cube + grayscale ramp, renderer RGB |
| 12. Multi-Tab (P2-01) | TabManager, owner-drawn tab strip, status dots, close button, Ctrl+T new tab |
| 13. Settings Dialog (P2-03) | Font, colours, scrollback, paste delay, logging toggle; `settings_validate()` clamps ranges |
| 14. Session Logging (P2-04) | `ansi_strip()`, timestamped log files, configurable directory + name format |
| 15. Paste Confirmation (P2-05) | Threshold=64, newline detection, preview dialog, inter-line paste_delay_ms |
| 16. Zoom (P2-06) | Ctrl+=/-, Ctrl+Scroll; font clamped 6–72; gutter-free fit with fallback |
| 17. Hover Tooltips (P3-01) | `tooltip_build_text()` table format, TTN_NEEDTEXT, `[L] = toggle session logging` footnote |
| 18. VT Sequences (P3-03) | OSC 0/2 title, DECTCEM, alt screen (?1049), app cursor keys (?1), insert mode (4) |
| 19. Light/Dark Theme (P3-02) | DwmSetWindowAttribute, BT.709 luminance in `theme.c` |
| 20. Error Dialogs (P3-04/05) | MessageBox for connection/auth failures, double-click connect in session manager |
| 21. Final Audit | Zero lint warnings, app_cursor_keys respected in keyboard handler |

## Follow-Up Fixes (Post-Audit)

| # | Fix |
|---|-----|
| 1 | Zoom-out after zoom-in: expanded search from 10 to 66 steps with fallback |
| 2 | Tab indicators: 12×H (full inner height), 50% wider, equidistant 3px gaps |
| 3 | SSH key browse button: "..." opens file picker via GetOpenFileNameA |
| 4 | Default log directory: exe directory via GetModuleFileNameA |
| 5 | Log format tooltip: strftime specifier list on hover (balloon tooltip) |
| 6 | Tab tooltip: table format with Host/User/Status/Logging + [L] footnote |
| 7 | Default scrollback: 10,000 lines (was 3,000) |
| 8 | Foreground/background colour: parse_hex_color + apply_config_colors wired in WM_CREATE and settings |

## Outstanding (Not Started)

- [ ] Optimize rendering (dirty rect tracking, partial redraws)
- [ ] Dr. Memory audit on Windows release build
- [ ] Server compatibility matrix: OpenSSH 8.x/9.x, Dropbear, Windows OpenSSH
- [ ] Address items in `vulnerabilities.md`
