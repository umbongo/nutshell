package ui

import (
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

	assert.NotNil(t, tab.tabContainer, "tab container should not be nil")
	assert.NotNil(t, tab.tabBtn, "tab button should not be nil")
	assert.NotNil(t, tab.statusIndicator, "status indicator should not be nil")
}

// Task 3: L badge field exists and is initialised.
func TestNewSessionTab_HasLoggingLabel(t *testing.T) {
	tab, _ := newTestSessionTab(t)
	assert.NotNil(t, tab.loggingLabel, "loggingLabel should not be nil")
}

// Task 3: SetLoggingState changes the badge background colour.
func TestSessionTab_SetLoggingState_UpdatesColor(t *testing.T) {
	tab, _ := newTestSessionTab(t)

	tab.SetLoggingState(true)
	assert.Equal(t, theme.SuccessColor(), tab.loggingBg.FillColor,
		"active logging should be green")

	tab.SetLoggingState(false)
	assert.Equal(t, theme.DisabledColor(), tab.loggingBg.FillColor,
		"inactive logging should be grey")
}

// Task 3+4: HBox is [tabBtn, loggingBadge, statusIndicator].
func TestSessionTab_TabHBoxOrder(t *testing.T) {
	tab, _ := newTestSessionTab(t)

	c, ok := tab.tabContainer.(*fyne.Container)
	require.True(t, ok, "tabContainer should be a *fyne.Container")

	hbox, ok := c.Objects[0].(*fyne.Container)
	require.True(t, ok, "first child of tabContainer should be the HBox *fyne.Container")
	require.Len(t, hbox.Objects, 3, "HBox should have 3 children: tabBtn, loggingBadge, statusIndicator")

	assert.Equal(t, tab.tabBtn, hbox.Objects[0], "first HBox item should be tabBtn")
	// Second item is the L badge container.
	_, ok = hbox.Objects[1].(*fyne.Container)
	assert.True(t, ok, "second HBox item should be the logging badge *fyne.Container")
	assert.Equal(t, tab.statusIndicator, hbox.Objects[2], "third HBox item should be statusIndicator")
}

// Task 6: tabLabel renders text at 80 % of the theme font size.
func TestTabLabel_TextSizeIsEightyPercent(t *testing.T) {
	test.NewApp()
	lbl := newTabLabel("hello")
	expected := theme.TextSize() * 0.8
	assert.InDelta(t, float64(expected), float64(lbl.TextSize()), 0.01,
		"tab label text size should be 80%% of the theme size")
}

// Task 5: tooltipText contains the expected fields.
func TestSessionTab_TooltipText(t *testing.T) {
	test.NewApp()
	w := test.NewWindow(nil)
	defer w.Close()
	profile := config.Profile{
		Name:     "Test",
		Username: "alice",
		Host:     "example.com",
		Port:     2222,
	}
	tab := NewSessionTab(profile, &config.Config{}, w, func() {})

	text := tab.tooltipText()
	assert.Contains(t, text, "alice", "tooltip should contain username")
	assert.Contains(t, text, "example.com", "tooltip should contain host")
	assert.Contains(t, text, "2222", "tooltip should contain port")
	assert.Contains(t, text, "Not logging", "tooltip should indicate no active log")
}
