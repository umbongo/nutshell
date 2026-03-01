package ui

import (
	"conga.ssh/internal/config"
	"log"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/driver/desktop"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
)


// App is the top-level application controller.
type App struct {
	fyneApp     fyne.App
	window      fyne.Window
	cfg         *config.Config
	sessions    []*SessionTab
	termTheme   *terminalTheme
	tabHBox     *fyne.Container   // HBox of tab buttons
	tabScroll   *container.Scroll // horizontal scroll wrapping tabHBox
	contentArea *fyne.Container   // shows the active session's content
	activeIdx   int               // index of the selected tab (-1 = none)
}

// NewApp creates and initialises the Conga.SSH application.
func NewApp(cfg *config.Config) *App {
	a := app.New()

	th := NewTerminalTheme(cfg)
	a.Settings().SetTheme(th)

	w := a.NewWindow("Conga.SSH")
	w.Resize(fyne.NewSize(1024, 680))
	w.CenterOnScreen()

	return &App{
		fyneApp:   a,
		window:    w,
		cfg:       cfg,
		termTheme: th,
		activeIdx: -1,
	}
}

// Run builds the UI and starts the event loop.
func (a *App) Run() {
	a.buildUI()
	a.window.ShowAndRun()
}

func (a *App) buildUI() {
	// Tab strip: scrollable HBox of tab buttons.
	a.tabHBox = container.NewHBox()
	a.tabScroll = container.NewHScroll(a.tabHBox)

	// Content area: shows the active session's terminal (or error view).
	a.contentArea = container.NewStack()

	// Toolbar buttons.
	plusBtn := widget.NewButton("", func() { a.openConnectDialog() })
	plusBtn.SetIcon(theme.ContentAddIcon())

	leftArrow := widget.NewButton("", func() { a.navigatePrev() })
	leftArrow.SetIcon(theme.NavigateBackIcon())

	rightArrow := widget.NewButton("", func() { a.navigateNext() })
	rightArrow.SetIcon(theme.NavigateNextIcon())

	settingsBtn := widget.NewButton("", func() {
		NewSettingsDialog(a.window, a.fyneApp, a.cfg, a.termTheme).Show()
	})
	settingsBtn.SetIcon(theme.SettingsIcon())

	// Single-line header: [+][<]  [tabs…]  [>][⚙]
	leftSide := container.NewHBox(plusBtn, leftArrow)
	rightSide := container.NewHBox(rightArrow, settingsBtn)
	header := container.NewBorder(nil, nil, leftSide, rightSide, a.tabScroll)

	// Separator line between the tab strip and the session content area.
	topSection := container.NewVBox(header, widget.NewSeparator())
	content := container.NewBorder(topSection, nil, nil, nil, a.contentArea)
	a.window.SetContent(content)

	a.window.SetOnClosed(func() {
		for _, s := range a.sessions {
			s.Close()
		}
	})

	a.registerShortcuts()
	a.openConnectDialog()
}

func (a *App) registerShortcuts() {
	a.window.Canvas().AddShortcut(
		&desktop.CustomShortcut{KeyName: fyne.KeyUp, Modifier: fyne.KeyModifierControl},
		func(_ fyne.Shortcut) {
			a.termTheme.AdjustFontSize(1)
			a.fyneApp.Settings().SetTheme(a.termTheme)
		},
	)
	a.window.Canvas().AddShortcut(
		&desktop.CustomShortcut{KeyName: fyne.KeyDown, Modifier: fyne.KeyModifierControl},
		func(_ fyne.Shortcut) {
			a.termTheme.AdjustFontSize(-1)
			a.fyneApp.Settings().SetTheme(a.termTheme)
		},
	)
}

func (a *App) openConnectDialog() {
	NewSessionManager(a.window, a.fyneApp, a.cfg, func(profile config.Profile) {
		a.openSession(profile)
	}).Show()
}

func (a *App) openSession(profile config.Profile) {
	var tab *SessionTab
	tab = NewSessionTab(profile, a.cfg, a.window, func() {
		a.removeSessionByTab(tab)
	})
	tab.tabBtn.OnTapped = func() { a.selectSessionByTab(tab) }
	tab.onZoom = func(delta float32) {
		a.termTheme.AdjustFontSize(delta)
		a.fyneApp.Settings().SetTheme(a.termTheme)
	}
	tab.onContentChange = func(c fyne.CanvasObject) {
		if a.activeIdx >= 0 && a.activeIdx < len(a.sessions) && a.sessions[a.activeIdx] == tab {
			a.contentArea.Objects = []fyne.CanvasObject{c}
			a.contentArea.Refresh()
		}
	}

	// Tapping the logging badge toggles logging on/off.
	tab.loggingLabel.OnTapped = func() {
		if tab.logger == nil {
			return
		}
		if tab.logger.IsEnabled() {
			tab.logger.Stop()
			tab.SetLoggingState(false)
		} else {
			if err := tab.logger.Start(); err != nil {
				log.Printf("failed to start logger: %v", err)
			} else {
				tab.SetLoggingState(true)
			}
		}
	}

	// Hover overlay: shows tooltip; no right-click menu.
	overlay := newTabHoverOverlay(
		func() string { return tab.tooltipText() },
		a.window.Canvas(),
	)
	tab.tabContainer.(*fyne.Container).Add(overlay)

	a.sessions = append(a.sessions, tab)
	a.tabHBox.Add(tab.tabContainer)
	a.tabHBox.Refresh()
	a.selectSessionByTab(tab)
	tab.Connect()
}

func (a *App) selectSessionByTab(tab *SessionTab) {
	for i, s := range a.sessions {
		if s == tab {
			a.selectSession(i)
			return
		}
	}
}

func (a *App) selectSession(idx int) {
	// Deselect current tab button.
	if a.activeIdx >= 0 && a.activeIdx < len(a.sessions) {
		a.sessions[a.activeIdx].tabBtn.Importance = widget.LowImportance
		a.sessions[a.activeIdx].tabBtn.Refresh()
	}
	a.activeIdx = idx
	if idx >= 0 && idx < len(a.sessions) {
		a.sessions[idx].tabBtn.Importance = widget.HighImportance
		a.sessions[idx].tabBtn.Refresh()
		a.contentArea.Objects = []fyne.CanvasObject{a.sessions[idx].content}
		a.contentArea.Refresh()
	} else {
		a.contentArea.Objects = nil
		a.contentArea.Refresh()
	}
}

func (a *App) removeSessionByTab(tab *SessionTab) {
	for i, s := range a.sessions {
		if s == tab {
			a.removeSession(i)
			return
		}
	}
}

// navigatePrev selects the session to the left of the current one.
func (a *App) navigatePrev() {
	if a.activeIdx > 0 {
		a.selectSession(a.activeIdx - 1)
	}
}

// navigateNext selects the session to the right of the current one.
func (a *App) navigateNext() {
	if a.activeIdx < len(a.sessions)-1 {
		a.selectSession(a.activeIdx + 1)
	}
}

func (a *App) removeSession(idx int) {
	if idx < 0 || idx >= len(a.sessions) {
		return
	}
	tab := a.sessions[idx]
	a.tabHBox.Remove(tab.tabContainer)
	a.tabHBox.Refresh()
	a.sessions = append(a.sessions[:idx], a.sessions[idx+1:]...)

	if a.activeIdx == idx {
		if len(a.sessions) > 0 {
			newIdx := idx
			if newIdx >= len(a.sessions) {
				newIdx = len(a.sessions) - 1
			}
			a.selectSession(newIdx)
		} else {
			a.activeIdx = -1
			a.contentArea.Objects = nil
			a.contentArea.Refresh()
		}
	} else if a.activeIdx > idx {
		a.activeIdx--
	}
}
