package ui

import (
	"testing"

	"conga.ssh/internal/config"
	"fyne.io/fyne/v2/test"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestNewSessionManager_NotNil(t *testing.T) {
	a := test.NewApp()
	w := test.NewWindow(nil)
	defer w.Close()
	sm := NewSessionManager(w, a, &config.Config{}, func(config.Profile) {})
	assert.NotNil(t, sm)
}

func TestSessionManager_ShowDoesNotPanic(t *testing.T) {
	a := test.NewApp()
	w := test.NewWindow(nil)
	defer w.Close()
	sm := NewSessionManager(w, a, &config.Config{}, func(config.Profile) {})
	assert.NotPanics(t, func() { sm.Show() })
}

func TestBuildProfileFromEntries_ValidPort(t *testing.T) {
	p := buildProfileFromEntries("srv", "host.com", "2222", "alice", "s3cr3t", "Password", "")
	require.Equal(t, "srv", p.Name)
	require.Equal(t, "host.com", p.Host)
	require.Equal(t, 2222, p.Port)
	require.Equal(t, "alice", p.Username)
	require.Equal(t, "s3cr3t", p.Password)
	require.Equal(t, config.AuthPassword, p.AuthType)
}

func TestBuildProfileFromEntries_InvalidPortDefaultsTo22(t *testing.T) {
	p := buildProfileFromEntries("x", "h", "notanumber", "u", "", "Password", "")
	assert.Equal(t, 22, p.Port)
}

func TestBuildProfileFromEntries_PortOutOfRangeDefaultsTo22(t *testing.T) {
	p := buildProfileFromEntries("x", "h", "99999", "u", "", "Password", "")
	assert.Equal(t, 22, p.Port)
}

func TestBuildProfileFromEntries_AuthKey(t *testing.T) {
	p := buildProfileFromEntries("k", "h", "22", "u", "passphrase", "SSH Key", "/home/u/.ssh/id_rsa")
	assert.Equal(t, config.AuthKey, p.AuthType)
	assert.Equal(t, "/home/u/.ssh/id_rsa", p.KeyPath)
	assert.Equal(t, "passphrase", p.Password)
}

func TestSessionManager_OnConnectCallback(t *testing.T) {
	a := test.NewApp()
	w := test.NewWindow(nil)
	defer w.Close()

	var got config.Profile
	called := false
	sm := NewSessionManager(w, a, &config.Config{}, func(p config.Profile) {
		got = p
		called = true
	})

	// Directly invoke the connect path with a known profile.
	sm.onConnect(config.Profile{Name: "Test", Host: "example.com", Port: 22, Username: "bob"})
	assert.True(t, called)
	assert.Equal(t, "example.com", got.Host)
}
