package ui

import (
	"testing"

	"conga.ssh/internal/config"
	"fyne.io/fyne/v2/test"
	"github.com/stretchr/testify/assert"
)

func TestNewSessionTab(t *testing.T) {
	// Setup a test app and window
	test.NewApp()
	w := test.NewWindow(nil)
	defer w.Close()

	// Create a dummy profile and config
	profile := config.Profile{
		Name:     "TestProfile",
		Host:     "localhost",
		Port:     22,
		Username: "testuser",
	}
	cfg := &config.Config{}

	// Create a new session tab
	tab := NewSessionTab(profile, cfg, w, func() {})

	// Assert that the tab container and its components are not nil
	assert.NotNil(t, tab.tabContainer, "tab container should not be nil")
	assert.NotNil(t, tab.tabBtn, "tab button should not be nil")
	assert.NotNil(t, tab.statusIndicator, "status indicator should not be nil")

	// You could add more assertions here to check the hierarchy, e.g.:
	// stack, ok := tab.tabContainer.(*container.Stack)
	// assert.True(t, ok, "tab container should be a stack")
	// assert.Len(t, stack.Objects, 2, "stack should have 2 children")
}
