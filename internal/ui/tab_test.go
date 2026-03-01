package ui

import (
	"image/color"
	"testing"

	"conga.ssh/internal/config"
	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/test"
	"fyne.io/fyne/v2/theme"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func newTestSessionTab(t *testing.T) (*SessionTab, fyne.Window) {
	t.Helper()
	test.NewApp()
	w := test.NewWindow(nil)
	t.Cleanup(func() { w.Close() })
	profile := config.Profile{
		Name:     "TestProfile",
		Host:     "localhost",
		Port:     22,
		Username: "testuser",
	}
	return NewSessionTab(profile, &config.Config{}, w, func() {}), w
}

func TestNewSessionTab(t *testing.T) {
	tab, _ := newTestSessionTab(t)
	assert.NotNil(t, tab.tabContainer)
	assert.NotNil(t, tab.tabBtn)
	assert.NotNil(t, tab.statusIndicator)
}

// Task 1: Tab container exposes a border rectangle with a visible stroke.
func TestSessionTab_TabHasBorder(t *testing.T) {
	tab, _ := newTestSessionTab(t)
	require.NotNil(t, tab.tabBorder)
	assert.Greater(t, tab.tabBorder.StrokeWidth, float32(0))
}

// Task 2: "L" text is always black regardless of logging state.
func TestSessionTab_LoggingBadgeLabelIsBlack(t *testing.T) {
	tab, _ := newTestSessionTab(t)
	tab.SetLoggingState(false)
	assert.Equal(t, color.Black, tab.loggingLabel.lTxt.Color)
	tab.SetLoggingState(true)
	assert.Equal(t, color.Black, tab.loggingLabel.lTxt.Color)
}

// Task 3/4: loggingLabel exists and implements fyne.Tappable.
func TestNewSessionTab_HasLoggingLabel(t *testing.T) {
	tab, _ := newTestSessionTab(t)
	assert.NotNil(t, tab.loggingLabel)
}

func TestSessionTab_LoggingBadgeIsTappable(t *testing.T) {
	tab, _ := newTestSessionTab(t)
	var obj fyne.CanvasObject = tab.loggingLabel
	_, ok := obj.(fyne.Tappable)
	assert.True(t, ok)
}

// SetLoggingState changes the badge background colour.
func TestSessionTab_SetLoggingState_UpdatesColor(t *testing.T) {
	tab, _ := newTestSessionTab(t)
	tab.SetLoggingState(true)
	assert.Equal(t, theme.SuccessColor(), tab.loggingLabel.bg.FillColor)
	tab.SetLoggingState(false)
	assert.Equal(t, theme.DisabledColor(), tab.loggingLabel.bg.FillColor)
}

// HBox order: [tabBtn, loggingBadge, statusIndicator].
func TestSessionTab_TabHBoxOrder(t *testing.T) {
	tab, _ := newTestSessionTab(t)

	_, ok := tab.tabContainer.(*fyne.Container)
	require.True(t, ok)
	require.NotNil(t, tab.tabHBox)
	require.Len(t, tab.tabHBox.Objects, 3)

	assert.Equal(t, tab.tabBtn, tab.tabHBox.Objects[0])
	_, ok = tab.tabHBox.Objects[1].(*loggingBadge)
	assert.True(t, ok, "second HBox item should be *loggingBadge")
	assert.Equal(t, tab.statusIndicator, tab.tabHBox.Objects[2])
}

// tabLabel renders text at 80% of theme font size.
func TestTabLabel_TextSizeIsEightyPercent(t *testing.T) {
	test.NewApp()
	lbl := newTabLabel("hello")
	assert.InDelta(t, float64(theme.TextSize()*0.8), float64(lbl.TextSize()), 0.01)
}

// tooltipText contains expected fields.
func TestSessionTab_TooltipText(t *testing.T) {
	test.NewApp()
	w := test.NewWindow(nil)
	defer w.Close()
	tab := NewSessionTab(config.Profile{
		Name: "Test", Username: "alice", Host: "example.com", Port: 2222,
	}, &config.Config{}, w, func() {})
	text := tab.tooltipText()
	assert.Contains(t, text, "alice")
	assert.Contains(t, text, "example.com")
	assert.Contains(t, text, "2222")
	assert.Contains(t, text, "Not logging")
}
