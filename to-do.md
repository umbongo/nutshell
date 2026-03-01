# To-Do

## Done
- [x] Host Key Verification (TOFU) — implemented and documented.
- [x] Session tab black-tab bug — session name now visible.

---

## Bug: Session tab shows no name (black tab)
**Status:** Open
**Context:** After recent changes to `app.go`/`tab.go`, the tab strip shows a blank black tab instead of the session name.
**Root cause area:** `openSession` in `internal/ui/app.go` — check that the HBox (indicator + label) is visible in the rendered tab container.
**Acceptance:** Tab strip displays the session name (e.g. `user@host` or profile name) for every open session.

---

## Feature: Interface overhaul (branch `interface1`)

### Prerequisites
- Create and switch to a new git branch named `interface1` before making any changes.
- Use TDD: write a failing test first, then implement, then verify the test passes.
- Update README.md development-status table and any affected docs after all tests pass.

### Tasks (implement in order)

1. **Vertical scrollbar on terminal**
   Add a vertical scrollbar to the terminal area so the user can scroll back through session output.

2. **Right-click context menu on session tab — logging toggle**
   Right-clicking a session tab shows a context menu with exactly one item:
   - "Start Logging" when logging is inactive for that session.
   - "Stop Logging" when logging is active for that session.
   The menu item must reflect live state (not stale).

3. **Logging status indicator on tab — "L" badge**
   Each tab displays a letter `L` to the right of the session name:
   - Green `L` when session logging is active.
   - Grey `L` when session logging is inactive.
   The indicator updates immediately when logging starts or stops.

4. **Connection status indicator on tab — coloured dot**
   Each tab displays a dot to the right of the `L` badge:
   - Green dot when the SSH session is connected.
   - Red dot when disconnected or in an error state.
   Both the `L` and the dot sit on the **right-hand side** of the tab, after the session name.

5. **Tab hover tooltip — session details**
   Hovering over a session tab shows a tooltip containing:
   - Logged-in username
   - Remote host and port
   - Active log file path (or "Not logging" if inactive)
   - Connection duration (e.g. `Connected 4m 32s`)

6. **Reduce tab label font size by 20%**
   The session name text in each tab must render at 80% of its current size.
   This applies to all tabs consistently; do not hard-code a pixel size — derive it from the current theme font size.

7. **Settings window — copyright footer**
   Add a non-interactive footer at the bottom of the settings window containing the text:
   `© 2026 Thomas Sulkiewicz`
   - The footer must be visually separated from the settings content (e.g. a separator line above it).
   - It must appear in `internal/ui/settings_dialog.go`, inside the existing `Show()` layout.
   - Font size and colour should follow the active theme (no hard-coded values).
   - No test required (purely cosmetic, no logic).
