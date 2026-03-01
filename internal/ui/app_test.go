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
	app := newTestApp(&config.Config{})
	app.openSession(config.Profile{Name: "Test"})

	require.Len(t, app.sessions, 1)
	tab := app.sessions[0]

	stack, ok := tab.tabContainer.(*fyne.Container)
	require.True(t, ok, "tabContainer should be *fyne.Container")

	// Stack: [0] border box, [1] hover overlay added by openSession.
	assert.Len(t, stack.Objects, 2)
	_, ok = stack.Objects[0].(*fyne.Container)
	assert.True(t, ok, "first child should be the border container")
}

// Task 6: navigatePrev/navigateNext switch the active session.
func TestApp_NavigatePrev_MovesToPreviousSession(t *testing.T) {
	a := newTestApp(&config.Config{})
	a.openSession(config.Profile{Name: "S1"})
	a.openSession(config.Profile{Name: "S2"})
	a.openSession(config.Profile{Name: "S3"})

	a.selectSession(2)
	a.navigatePrev()
	assert.Equal(t, 1, a.activeIdx)
	a.navigatePrev()
	assert.Equal(t, 0, a.activeIdx)
	a.navigatePrev() // already at first
	assert.Equal(t, 0, a.activeIdx)
}

func TestApp_NavigateNext_MovesToNextSession(t *testing.T) {
	a := newTestApp(&config.Config{})
	a.openSession(config.Profile{Name: "S1"})
	a.openSession(config.Profile{Name: "S2"})

	a.selectSession(0)
	a.navigateNext()
	assert.Equal(t, 1, a.activeIdx)
	a.navigateNext() // already at last
	assert.Equal(t, 1, a.activeIdx)
}
