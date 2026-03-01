# To-Do

## Done
- [x] Host Key Verification (TOFU) — implemented and documented.
- [x] Session tab black-tab bug — session name now visible.
- [x] Interface overhaul (`interface1`) — all 7 tasks shipped, README updated, branch merged to main.
- [x] Bug: Tooltip popup too small — switched to `TextWrapOff` so popup expands to content width.
- [x] Bug: Tab indicator boxes inconsistent — both "L" badge and connection dot are now uniform 16×16 boxes.
- [x] Feature: Session manager window (`session-manager`) — two-column window with profile list, CRUD, connect, merged to main.
- [x] Feature: Tab strip overhaul (`tab-strip-overhaul`) — bordered tabs, black L, click-to-toggle logging, separator, arrow-key session navigation.

---

## Feature: Tab strip overhaul
**Branch:** `tab-strip-overhaul`
**Status:** Complete — awaiting merge to main.

### Tasks
- [x] 1. Wrap each tab's session name, "L" badge, and connection dot inside a single bordered box.
- [x] 2. The "L" letter inside the logging badge is always rendered in black.
- [x] 3. Remove the right-click context menu from the tab strip entirely.
- [x] 4. Clicking the logging light ("L" badge) toggles logging on/off directly.
- [x] 5. Draw a separator line between the tab strip and the session output area.
- [x] 6. The left `<` and right `>` arrow buttons move focus to the previous/next session tab.

---

## Feature: Light and dark interface theme
**Branch:** `theme-switcher`
**Status:** Open

### Prerequisites
- Create and switch to branch `theme-switcher` before making any changes.
- Use TDD: write a failing test first, then implement, then verify the test passes.
- Update README.md development-status table and any affected docs after all tests pass.

### Tasks
- [ ] 1. Add a `Theme` setting to `Settings` (values: `"light"` | `"dark"` | `"system"`); persist to `settings.ini`.
- [ ] 2. Implement a light variant and a dark variant of the terminal theme colours (background, foreground, cursor).
- [ ] 3. Add a theme selector (e.g. radio group or select widget) to the Settings window.
- [ ] 4. Apply the chosen theme immediately when the selector changes; no restart required.
- [ ] 5. Default to `"system"` (follow OS dark/light preference) on first run.

---


## Feature: Interface overhaul (branch `interface1`)
**Branch:** `interface1` *(merged and deleted)*
**Status:** Complete

### Tasks
- [x] 1. Vertical scrollbar on terminal *(blocked — `fyne-io/terminal` has no public scrollback API)*
- [x] 2. Right-click context menu on session tab — "Start Logging" / "Stop Logging" toggle.
- [x] 3. "L" badge on tab — green when logging active, grey when inactive; updates immediately.
- [x] 4. Connection status dot on tab — green when connected, red when disconnected; sits right of "L".
- [x] 5. Hover tooltip — shows username, host:port, log file path, connection duration.
- [x] 6. Tab label font size at 80% of current theme size (derived, not hard-coded).
- [x] 7. Settings window copyright footer — `© 2026 Thomas Sulkiewicz`, separator above, theme-styled.
