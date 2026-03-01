package ui

import (
	"fmt"
	"strconv"

	"conga.ssh/internal/config"
	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/layout"
	"fyne.io/fyne/v2/widget"
)

// namedColour pairs a human-readable name with its hex value.
type namedColour struct {
	name string
	hex  string
}

var colourPalette = []namedColour{
	{"Light Gray", "#CCCCCC"},
	{"Black", "#0C0C0C"},
	{"Dark Green", "#13A10E"},
	{"Bright Green", "#16C60C"},
	{"Dark Blue", "#0037DA"},
	{"Cyan Blue", "#3A96DD"},
	{"Dark Red", "#C50F1F"},
	{"Dark Yellow", "#C19C00"},
	{"Dark Magenta", "#881798"},
	{"Medium Gray", "#767676"},
	{"White", "#F2F2F2"},
	{"Bright Yellow", "#F9F1A5"},
	{"Bright Cyan", "#61D6D6"},
	{"Bright Red", "#E74856"},
}

// colourNames returns the list of names for use in a Select widget.
func colourNames() []string {
	names := make([]string, len(colourPalette))
	for i, c := range colourPalette {
		names[i] = c.name
	}
	return names
}

// hexToName finds the colour name for a hex value, or returns the hex if not found.
func hexToName(hex string) string {
	for _, c := range colourPalette {
		if c.hex == hex {
			return c.name
		}
	}
	return hex
}

// nameToHex looks up a hex value by colour name, or returns name unchanged if not found.
func nameToHex(name string) string {
	for _, c := range colourPalette {
		if c.name == name {
			return c.hex
		}
	}
	return name
}

// SettingsDialog presents the application settings form.
type SettingsDialog struct {
	window  fyne.Window
	fyneApp fyne.App
	cfg     *config.Config
	theme   *terminalTheme
}

// NewSettingsDialog creates a SettingsDialog.
func NewSettingsDialog(w fyne.Window, a fyne.App, cfg *config.Config, th *terminalTheme) *SettingsDialog {
	return &SettingsDialog{window: w, fyneApp: a, cfg: cfg, theme: th}
}

// Show opens the settings in a dedicated window to avoid popup-within-dialog issues.
func (sd *SettingsDialog) Show() {
	w := sd.fyneApp.NewWindow("Settings")
	w.Resize(fyne.NewSize(480, 460))
	w.SetFixedSize(true)
	w.CenterOnScreen()

	// Font
	fontSel := widget.NewSelect([]string{
		"Consolas", "Courier New", "Lucida Console", "Cascadia Code", "Cascadia Mono",
	}, nil)
	fontSel.SetSelected(sd.cfg.Settings.Font)

	fontSizeEntry := widget.NewEntry()
	fontSizeEntry.SetText(fmt.Sprintf("%.0f", sd.theme.FontSize()))

	// Colours
	fgSel := widget.NewSelect(colourNames(), nil)
	fgSel.SetSelected(hexToName(sd.cfg.Settings.ForegroundColour))

	bgSel := widget.NewSelect(colourNames(), nil)
	bgSel.SetSelected(hexToName(sd.cfg.Settings.BackgroundColour))

	// Paste
	pasteDelayEntry := widget.NewEntry()
	pasteDelayEntry.SetText(strconv.Itoa(sd.cfg.Settings.PasteDelayMs))

	// Logging
	loggingCheck := widget.NewCheck("", nil)
	loggingCheck.SetChecked(sd.cfg.Settings.LoggingEnabled)

	logFormatEntry := widget.NewEntry()
	logFormatEntry.SetText(sd.cfg.Settings.LogFormat)

	logDirEntry := widget.NewEntry()
	logDirEntry.SetText(sd.cfg.Settings.LogDir)

	browseBtn := widget.NewButton("Browse...", func() {
		dialog.ShowFolderOpen(func(lu fyne.ListableURI, err error) {
			if err != nil || lu == nil {
				return
			}
			logDirEntry.SetText(lu.Path())
		}, w)
	})

	// Scrollback
	scrollbackEntry := widget.NewEntry()
	scrollbackEntry.SetText(strconv.Itoa(sd.cfg.Settings.ScrollbackLines))

	form := widget.NewForm(
		widget.NewFormItem("Font", fontSel),
		widget.NewFormItem("Font size", fontSizeEntry),
		widget.NewFormItem("Foreground colour", fgSel),
		widget.NewFormItem("Background colour", bgSel),
		widget.NewFormItem("Paste delay (ms)", pasteDelayEntry),
		widget.NewFormItem("Session logging", loggingCheck),
		widget.NewFormItem("Log format", logFormatEntry),
		widget.NewFormItem("Log directory",
			container.NewBorder(nil, nil, nil, browseBtn, logDirEntry)),
		widget.NewFormItem("Scrollback lines", scrollbackEntry),
	)

	saveBtn := widget.NewButton("Save", func() {
		sd.cfg.Settings.Font = fontSel.Selected
		if sz, err := strconv.ParseFloat(fontSizeEntry.Text, 32); err == nil && sz > 0 {
			sd.cfg.Settings.FontSize = sz
		}
		sd.cfg.Settings.ForegroundColour = nameToHex(fgSel.Selected)
		sd.cfg.Settings.BackgroundColour = nameToHex(bgSel.Selected)
		if d, err := strconv.Atoi(pasteDelayEntry.Text); err == nil && d >= 0 {
			sd.cfg.Settings.PasteDelayMs = d
		}
		sd.cfg.Settings.LoggingEnabled = loggingCheck.Checked
		sd.cfg.Settings.LogFormat = logFormatEntry.Text
		sd.cfg.Settings.LogDir = logDirEntry.Text
		if s, err := strconv.Atoi(scrollbackEntry.Text); err == nil && s > 0 {
			sd.cfg.Settings.ScrollbackLines = s
		}
		_ = sd.cfg.Save()
		sd.theme.Apply(sd.cfg)
		sd.fyneApp.Settings().SetTheme(sd.theme)
		w.Close()
	})
	saveBtn.Importance = widget.HighImportance

	cancelBtn := widget.NewButton("Cancel", func() {
		w.Close()
	})

	buttons := container.New(layout.NewHBoxLayout(),
		layout.NewSpacer(), cancelBtn, saveBtn,
	)

	copyright := widget.NewLabel("© 2026 Thomas Sulkiewicz")
	copyright.Alignment = fyne.TextAlignCenter
	footer := container.NewVBox(widget.NewSeparator(), copyright)

	content := container.NewBorder(nil,
		container.NewVBox(footer, buttons),
		nil, nil,
		container.NewScroll(form),
	)
	w.SetContent(content)
	w.Show()
}
