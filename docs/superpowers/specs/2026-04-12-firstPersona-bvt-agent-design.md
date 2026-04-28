# firstPersona — Automated BVT Agent for Nutshell

**Location:** `nutshell/firstPersona/`  
**Status:** Specification v0.1  
**Author:** Thomas (CSAO / WSU)  
**Purpose:** First-person automated testing agent for Nutshell UI and behaviour validation

---

## 1. Overview

`firstPersona` is an automated testing subsystem that lives inside the Nutshell repository. It launches a real instance of `nutshell.exe`, navigates the application as a human user would, and reports bugs and UX issues discovered along the way.

The agent operates in the **first person** — it describes its experience from the perspective of a user sitting at the keyboard. Test results are written as first-person observations ("I clicked Connect and the window went blank") rather than assertion-style pass/fail output. This makes issues easier to interpret and closer to what a real bug report would contain.

The primary use case for the initial release is **Build Verification Testing (BVT)** — a structured sweep of core user journeys after each build, designed to catch regressions in rendering, input handling, and basic SSH connectivity before manual testing begins.

---

## 2. Goals

- Launch a real `nutshell.exe` instance and interact with it programmatically
- Navigate through a configurable set of test cases using Win32 UI Automation
- Capture screenshots at key steps and feed them to a vision-capable LLM
- Produce first-person narrative test reports describing what was seen and experienced
- Flag rendering artefacts, unexpected behaviour, and UX friction points
- Be easy to run locally by any developer with Python installed

---

## 3. Non-Goals (v0.1)

- No CI/CD integration (future milestone)
- No SSH connectivity testing against live servers (mocked or skipped in BVT)
- No performance benchmarking
- No accessibility (a11y) testing
- No cross-OS or cross-version matrix testing

---

## 4. Repository Layout

```
nutshell/
└── firstPersona/
    ├── README.md               # How to run
    ├── requirements.txt        # Python dependencies
    ├── config.py               # Paths, model selection, timeouts
    ├── agent.py                # Main entry point — orchestrates test runs
    ├── launcher.py             # Launches and attaches to nutshell.exe
    ├── ui.py                   # UI Automation helpers (click, type, query tree)
    ├── vision.py               # Screenshot capture + LLM vision calls
    ├── reporter.py             # Formats and writes the test report
    ├── tests/
    │   ├── __init__.py
    │   ├── bvt_suite.py        # Built-in BVT test suite (see Section 7)
    │   └── custom/             # Drop-in folder for user-defined test scripts
    └── reports/
        └── .gitkeep            # Output directory for generated reports
```

---

## 5. Architecture

```
agent.py
  │
  ├── launcher.py ──────────────► nutshell.exe (subprocess)
  │
  ├── ui.py ────────────────────► pywinauto / uiautomation
  │       (click, type, read UI tree, wait for controls)
  │
  ├── vision.py ────────────────► pyautogui (screenshot)
  │       │                    └► Anthropic API (claude-sonnet-4)
  │       └── returns: LLM description of what is on screen
  │
  └── reporter.py ──────────────► Markdown report in reports/
```

The agent drives the test cases step by step. At each step it can:
- Perform a UI action (click, type, resize, scroll)
- Capture a screenshot and ask the LLM what it sees
- Record the LLM's first-person observation as a test step result
- Decide to continue, flag an issue, or abort the current test case

---

## 6. Runtime Configuration (`config.py`)

| Key | Default | Description |
|-----|---------|-------------|
| `NUTSHELL_EXE` | `../build/nutshell.exe` | Path to the binary under test |
| `MODEL` | `claude-sonnet-4-20250514` | LLM model for vision analysis |
| `SCREENSHOT_DIR` | `reports/screenshots/` | Where screenshots are saved |
| `STEP_DELAY_MS` | `400` | Pause between automated actions (ms) |
| `LAUNCH_TIMEOUT_S` | `5` | How long to wait for the window to appear |
| `CONFIRM_EACH_STEP` | `False` | Interactive mode — pause for human approval |
| `LIVE_VIEWER` | `True` | Show a real-time screenshot viewer window |

---

## 7. BVT Test Suite (`tests/bvt_suite.py`)

Each test case below maps to a function in `bvt_suite.py`. Test cases run in sequence. Each one produces a set of first-person observations and an overall result (`PASS`, `WARN`, or `FAIL`).

### TC-01 — Application Launch

**What I do:** I launch Nutshell and wait for the main window to appear.

Steps:
1. Execute `nutshell.exe`
2. Wait for the main window to become visible (up to `LAUNCH_TIMEOUT_S`)
3. Capture screenshot
4. Ask LLM: *"Describe what you see. Does the window appear complete and correctly rendered? Note any visual artefacts, missing elements, or unexpected blank areas."*

Pass criteria: Window is visible, title bar is present, main UI controls are rendered.

---

### TC-02 — Window Resize Behaviour

**What I do:** I resize the Nutshell window through a range of sizes and look for rendering problems.

Steps:
1. Resize to minimum viable size (~400×300)
2. Capture screenshot + LLM observation
3. Resize to a large size (~1600×1000)
4. Capture screenshot + LLM observation
5. Restore to default size
6. Capture screenshot + LLM observation

Ask LLM at each size: *"Describe what you see. Are all controls visible and correctly laid out? Is any text clipped, overlapping, or garbled? Are there any blank or unpainted regions?"*

Pass criteria: No rendering artefacts, controls remain proportional, no GDI paint failures.

---

### TC-03 — Window Minimise and Restore

**What I do:** I minimise the window to the taskbar, wait a moment, then restore it.

Steps:
1. Minimise the window
2. Wait 1 second
3. Restore the window
4. Capture screenshot
5. Ask LLM: *"Does the window look correct after being restored from the taskbar? Is there any visual corruption, ghost painting, or incomplete redraw?"*

Pass criteria: Window repaints fully on restore with no artefacts.

---

### TC-04 — Connect Dialog — Field Interaction

**What I do:** I open the connection dialog and interact with each input field.

Steps:
1. Trigger the Connect/New Connection action (button or menu)
2. Capture screenshot + LLM observation of the dialog
3. Click the Host/IP field and type a test hostname (`test.example.com`)
4. Tab to the Port field, clear it, type `2222`
5. Tab to the Username field, type `testuser`
6. Capture screenshot
7. Ask LLM: *"Describe the connection dialog. Is the text I typed visible and correctly rendered in each field? Does anything look wrong with the input controls?"*

Pass criteria: All fields accept and display input correctly.

---

### TC-05 — Connect Dialog — Cancel / Close

**What I do:** I open the connection dialog and dismiss it without connecting.

Steps:
1. Open the connection dialog
2. Click Cancel (or press Escape)
3. Capture screenshot
4. Ask LLM: *"Has the dialog been dismissed? Is the main window in a clean state?"*

Pass criteria: Dialog closes cleanly, main window returns to idle state with no residual artefacts.

---

### TC-06 — Terminal Pane Rendering

**What I do:** I inspect the terminal pane area for baseline rendering quality.

Steps:
1. Focus the terminal/output pane
2. Capture a zoomed-in screenshot of just that region
3. Ask LLM: *"Describe the terminal pane. Is the font rendering clear and sharp? Is there correct contrast between text and background? Are there any obvious pixel artefacts, blurry regions, or rendering inconsistencies?"*

Pass criteria: Terminal pane renders with sharp text, no obvious GDI quality issues.

---

### TC-07 — Menu Navigation

**What I do:** I open each top-level menu item and observe the dropdown.

Steps:
1. For each menu in the menu bar:
   a. Click the menu item
   b. Capture screenshot
   c. Ask LLM: *"Is the dropdown menu rendered correctly? Are the items readable? Is anything clipped or misaligned?"*
   d. Press Escape to close
2. Aggregate observations

Pass criteria: All menus open and close cleanly, all items are visible and readable.

---

### TC-08 — Settings / Preferences Dialog

**What I do:** I open the Settings or Preferences dialog and inspect it.

Steps:
1. Open Settings (via menu or keyboard shortcut)
2. Capture screenshot
3. Ask LLM: *"Describe the Settings dialog. Are all controls visible? Is text readable? Is anything misaligned or visually broken?"*
4. Close the dialog
5. Capture screenshot + LLM check of main window state

Pass criteria: Settings dialog renders fully, closes cleanly.

---

### TC-09 — Dark / Light Theme Toggle (if applicable)

**What I do:** If Nutshell supports theme switching, I toggle the theme and check for rendering issues.

Steps:
1. Switch to dark mode (or light, depending on current state)
2. Capture screenshot
3. Ask LLM: *"Has the theme changed? Are there any controls that haven't updated their colours — text that's invisible, controls that look wrong, or mismatched elements?"*
4. Switch back to original theme

Pass criteria: Theme applies consistently across all UI elements.

---

### TC-10 — Application Close

**What I do:** I close Nutshell cleanly and confirm it exits without errors.

Steps:
1. Press Alt+F4 or click the close button
2. Handle any confirmation dialogs (accept/confirm close)
3. Verify the process has exited (check via `launcher.py`)

Pass criteria: Application closes without hanging, no crash dialogs appear.

---

## 8. First-Person Reporting

After each test case, `reporter.py` writes an entry in the following format:

```markdown
## TC-02 — Window Resize Behaviour
**Result:** WARN
**Duration:** 4.2s

### Steps

**Step 1 — Resized to 400×300**
I resized the window down to its minimum size. The main panel compressed as expected, 
but I noticed the bottom toolbar did not repaint correctly — there's a grey stripe 
where the status bar should be.
📸 Screenshot: reports/screenshots/tc02_step1.png

**Step 2 — Resized to 1600×1000**
At the larger size, everything looked correct. The terminal pane expanded to fill 
the available space and the font remained sharp.
📸 Screenshot: reports/screenshots/tc02_step2.png

**Step 3 — Restored to default size**
On restoring to the default size, the status bar rendered correctly again. 
The issue appears to be specific to minimum-size rendering.
📸 Screenshot: reports/screenshots/tc02_step3.png

### Observations
- Possible WM_PAINT not firing on resize below a certain threshold
- Status bar GDI region may not be invalidated correctly on downsize
```

The full report is written to `reports/bvt_YYYYMMDD_HHMMSS.md`.

---

## 9. Python Dependencies (`requirements.txt`)

```
anthropic>=0.25.0
pywinauto>=0.6.8
pyautogui>=0.9.54
Pillow>=10.0.0
uiautomation>=2.0.18
```

> **Note:** `uiautomation` requires Windows. This tooling is Windows-only by design.

---

## 10. Usage

```bash
# Install dependencies
cd firstPersona
pip install -r requirements.txt

# Run the full BVT suite
python agent.py --suite bvt

# Run a single test case
python agent.py --test TC-02

# Run in interactive mode (confirm each step)
python agent.py --suite bvt --confirm

# Run with live viewer window
python agent.py --suite bvt --live-view

# Point at a specific binary
python agent.py --suite bvt --exe "C:\builds\nutshell_dev.exe"
```

---

## 11. Adding Custom Test Cases

Drop a Python file into `tests/custom/`. Each file should define a class inheriting from `TestCase`:

```python
from firstPersona.tests import TestCase, Step, Result

class MyCustomTest(TestCase):
    id = "TC-CUSTOM-01"
    name = "My Custom Scenario"

    def run(self, ui, vision):
        yield Step("Open settings", ui.click("Settings"))
        obs = vision.observe("Does the settings dialog look correct?")
        yield Step("Check settings render", obs)
        return Result.PASS if "correct" in obs.lower() else Result.WARN
```

Run with: `python agent.py --test TC-CUSTOM-01`

---

## 12. Future Milestones

| Milestone | Description |
|-----------|-------------|
| v0.2 | GitHub Actions integration — run BVT on PR builds (Windows runner) |
| v0.3 | SSH connectivity test cases against a local test server (e.g. OpenSSH loopback) |
| v0.4 | Regression diffing — compare screenshots between builds to detect visual regressions |
| v0.5 | Structured issue export — write flagged issues directly to GitHub Issues via API |
