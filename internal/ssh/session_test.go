package ssh

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestNewSession_DefaultPort(t *testing.T) {
	cfg := SessionConfig{
		Host:     "example.com",
		Username: "user",
		Password: "pass",
	}
	s := NewSession(cfg)
	if s.config.Port != 22 {
		t.Errorf("expected default port 22, got %d", s.config.Port)
	}
}

func TestNewSession_CustomPort(t *testing.T) {
	cfg := SessionConfig{
		Host:     "example.com",
		Port:     2222,
		Username: "user",
		Password: "pass",
	}
	s := NewSession(cfg)
	if s.config.Port != 2222 {
		t.Errorf("expected port 2222, got %d", s.config.Port)
	}
}

func TestSessionConfig_Address(t *testing.T) {
	cfg := SessionConfig{Host: "myserver.local", Port: 22}
	s := NewSession(cfg)
	addr := s.Address()
	if addr != "myserver.local:22" {
		t.Errorf("expected myserver.local:22, got %s", addr)
	}
}

func TestSessionConfig_AddressCustomPort(t *testing.T) {
	cfg := SessionConfig{Host: "10.0.0.1", Port: 2222}
	s := NewSession(cfg)
	addr := s.Address()
	if addr != "10.0.0.1:2222" {
		t.Errorf("expected 10.0.0.1:2222, got %s", addr)
	}
}

func TestSession_InitialState(t *testing.T) {
	s := NewSession(SessionConfig{Host: "h", Port: 22, Username: "u", Password: "p"})
	if s.IsConnected() {
		t.Error("new session should not be connected")
	}
}

func TestSession_Connect_InvalidHost(t *testing.T) {
	cfg := SessionConfig{
		Host:     "256.256.256.256", // invalid IP
		Port:     22,
		Username: "user",
		Password: "pass",
	}
	s := NewSession(cfg)
	err := s.Connect()
	if err == nil {
		t.Error("expected error connecting to invalid host")
		s.Close()
	}
	if s.IsConnected() {
		t.Error("should not be connected after failed connect")
	}
}

func TestSession_Close_NotConnected(t *testing.T) {
	s := NewSession(SessionConfig{Host: "h", Port: 22, Username: "u", Password: "p"})
	// Should not panic or error
	s.Close()
}

func TestSession_KeyAuth_MissingKeyFile(t *testing.T) {
	cfg := SessionConfig{
		Host:    "256.256.256.256",
		Port:    22,
		Username: "user",
		KeyPath: "/nonexistent/path/to/key",
	}
	s := NewSession(cfg)
	err := s.Connect()
	if err == nil {
		t.Error("expected error for missing key file")
		s.Close()
		return
	}
	if !strings.Contains(err.Error(), "reading key file") {
		t.Errorf("expected 'reading key file' error, got: %v", err)
	}
}

func TestSession_KeyAuth_InvalidKey(t *testing.T) {
	dir := t.TempDir()
	keyPath := filepath.Join(dir, "invalid.key")
	if err := os.WriteFile(keyPath, []byte("this is not a valid private key"), 0600); err != nil {
		t.Fatal(err)
	}

	cfg := SessionConfig{
		Host:    "256.256.256.256",
		Port:    22,
		Username: "user",
		KeyPath: keyPath,
	}
	s := NewSession(cfg)
	err := s.Connect()
	if err == nil {
		t.Error("expected error for invalid key file")
		s.Close()
		return
	}
	if !strings.Contains(err.Error(), "parsing private key") {
		t.Errorf("expected 'parsing private key' error, got: %v", err)
	}
}
