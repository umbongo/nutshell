# To-Do

## Done
- [x] Host Key Verification (TOFU) — implemented and documented.
- [x] Session tab black-tab bug — session name now visible.
- [x] Interface overhaul (`interface1`) — all 7 tasks shipped, README updated.
- [x] Bug: Tooltip popup too small — switched to `TextWrapOff` so popup expands to content width.
- [x] Bug: Tab indicator boxes inconsistent — both "L" badge and connection dot are now uniform 16×16 boxes.
- [x] Feature: Session manager window (`session-manager`) — two-column window with profile list, CRUD, and connect.

---

## Feature: Light and dark interface theme
**Branch:** `theme-switcher`
**Status:** Open

### Prerequisites
- Create and switch to branch `theme-switcher` before making any changes.
- Use TDD: write a failing test first, then implement, then verify the test passes.
- Update README.md development-status table and any affected docs after all tests pass.

### Tasks
- [ ] 1. Add a "Theme" setting to `Settings` (values: `"light"` | `"dark"` | `"system"`); persist to `settings.ini`.
- [ ] 2. Implement a light variant and a dark variant of the terminal theme colours (background, foreground, cursor).
- [ ] 3. Add a theme selector (e.g. radio group or select widget) to the Settings window.
- [ ] 4. Apply the chosen theme immediately when the selector changes; no restart required.
- [ ] 5. Default to `"system"` (follow OS dark/light preference) on first run.

---

## Feature: Session manager window
**Branch:** `session-manager`
**Status:** Complete

### Prerequisites
- Create and switch to branch `session-manager` before making any changes.
- Use TDD: write a failing test first, then implement, then verify the test passes.
- Update README.md development-status table and any affected docs after all tests pass.

### Tasks
- [x] 1. Replace the `+` / connect dialog with a two-column session manager window (equal width columns).
- [x] 2. Left column — scrollable list of all saved profiles; clicking a profile selects it.
- [x] 3. Right column — "New connection" form: session name, hostname, port, username, password, key path.
- [x] 4. "Connect" button — opens the selected saved profile **or** the filled-in new-connection form as a session tab.
- [x] 5. "Save" button — persists a new or edited connection to the profile list in config; list refreshes immediately.
- [x] 6. "Delete" button — removes the selected saved profile after confirmation; list refreshes immediately.
- [x] 7. "Edit" button — populates the right-column form with the selected profile's details for modification.
- [x] 8. Ensure profiles are persisted to `settings.ini` and survive application restart.

---

## Feature: Interface overhaul (branch `interface1`)
**Branch:** `interface1`
**Status:** Complete

### Prerequisites
- Create and switch to branch `interface1` before making any changes.
- Use TDD: write a failing test first, then implement, then verify the test passes.
- Update README.md development-status table and any affected docs after all tests pass.

### Tasks
- [x] 1. Vertical scrollbar on terminal *(blocked — `fyne-io/terminal` has no public scrollback API)*
- [x] 2. Right-click context menu on session tab — "Start Logging" / "Stop Logging" toggle.
- [x] 3. "L" badge on tab — green when logging active, grey when inactive; updates immediately.
- [x] 4. Connection status dot on tab — green when connected, red when disconnected; sits right of "L".
- [x] 5. Hover tooltip — shows username, host:port, log file path, connection duration.
- [x] 6. Tab label font size at 80% of current theme size (derived, not hard-coded).
- [x] 7. Settings window copyright footer — `© 2026 Thomas Sulkiewicz`, separator above, theme-styled.
