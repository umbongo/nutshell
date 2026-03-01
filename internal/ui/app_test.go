package ui

import (
	"testing"

	"conga.ssh/internal/config"
	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/test"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// newTestApp builds a minimal App using the headless Fyne test driver so that
// no real window or connect dialog is shown during tests.
func newTestApp(cfg *config.Config) *App {
	a := test.NewApp()
	w := test.NewWindow(nil)
	th := NewTerminalTheme(cfg)
	return &App{
		fyneApp:     a,
		window:      w,
		cfg:         cfg,
		termTheme:   th,
		tabHBox:     container.NewHBox(),
		tabScroll:   container.NewHScroll(container.NewHBox()),
		contentArea: container.NewStack(),
		activeIdx:   -1,
	}
}

func TestApp_OpenSession_PreservesTabContainer(t *testing.T) {
	cfg := &config.Config{}
	app := newTestApp(cfg)

	profile := config.Profile{Name: "Test"}
	app.openSession(profile)

	require.Len(t, app.sessions, 1, "should have one session")
	tab := app.sessions[0]

	stack, ok := tab.tabContainer.(*fyne.Container)
	require.True(t, ok, "tab container should be a *fyne.Container")

	// The stack should contain:
	// 1. HBox with the status indicator and tab button (session name label)
	// 2. The right-click detector overlay (added by openSession)
	assert.Len(t, stack.Objects, 2, "stack should have two children")

	// First child is the HBox container holding the indicator and labelled button.
	_, ok = stack.Objects[0].(*fyne.Container)
	assert.True(t, ok, "first child should be the HBox container")
}
