package config

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"conga.ssh/internal/crypto"
)

func TestNewSettings_Defaults(t *testing.T) {
	s := newSettings()
	if s.Font != "Consolas" {
		t.Errorf("expected font Consolas, got %s", s.Font)
	}
	if s.FontSize != 12 {
		t.Errorf("expected font size 12, got %f", s.FontSize)
	}
	if s.ScrollbackLines != 3000 {
		t.Errorf("expected scrollback 3000, got %d", s.ScrollbackLines)
	}
	if s.PasteDelayMs != 350 {
		t.Errorf("expected paste delay 350, got %d", s.PasteDelayMs)
	}
	if s.LoggingEnabled != false {
		t.Error("expected logging disabled by default")
	}
	if s.LogFormat != "{date}_{time}_{host}.log" {
		t.Errorf("unexpected log format: %s", s.LogFormat)
	}
	if s.LogDir != "." {
		t.Errorf("expected log dir '.', got %s", s.LogDir)
	}
	if s.HostKeyVerification != false {
		t.Error("expected host key verification disabled by default")
	}
	if s.ForegroundColour != "#0C0C0C" {
		t.Errorf("expected fg #0C0C0C, got %s", s.ForegroundColour)
	}
	if s.BackgroundColour != "#F2F2F2" {
		t.Errorf("expected bg #F2F2F2, got %s", s.BackgroundColour)
	}
}

func TestLoad_CreatesFileIfMissing(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "settings.ini")

	cfg, err := Load(path, nil)
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}
	if _, err := os.Stat(path); os.IsNotExist(err) {
		t.Error("settings.ini was not created")
	}
	// Defaults should be present
	if cfg.Settings.Font != "Consolas" {
		t.Errorf("expected Consolas font, got %s", cfg.Settings.Font)
	}
}

func TestLoad_ReadsExistingFile(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "settings.ini")

	content := `[settings]
font = Courier New
font_size = 14
scrollback_lines = 5000
paste_delay_ms = 200
logging_enabled = true
log_format = {host}_{date}.log
log_dir = C:\logs
host_key_verification = true
foreground_colour = #00FF00
background_colour = #000000
`
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		t.Fatal(err)
	}

	cfg, err := Load(path, nil)
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}
	if cfg.Settings.Font != "Courier New" {
		t.Errorf("expected Courier New, got %s", cfg.Settings.Font)
	}
	if cfg.Settings.FontSize != 14 {
		t.Errorf("expected 14, got %f", cfg.Settings.FontSize)
	}
	if cfg.Settings.ScrollbackLines != 5000 {
		t.Errorf("expected 5000, got %d", cfg.Settings.ScrollbackLines)
	}
	if cfg.Settings.PasteDelayMs != 200 {
		t.Errorf("expected 200, got %d", cfg.Settings.PasteDelayMs)
	}
	if !cfg.Settings.LoggingEnabled {
		t.Error("expected logging enabled")
	}
	if cfg.Settings.LogFormat != "{host}_{date}.log" {
		t.Errorf("unexpected log format: %s", cfg.Settings.LogFormat)
	}
	if !cfg.Settings.HostKeyVerification {
		t.Error("expected host key verification enabled")
	}
	if cfg.Settings.ForegroundColour != "#00FF00" {
		t.Errorf("expected #00FF00, got %s", cfg.Settings.ForegroundColour)
	}
}

func TestSave_PersistsSettings(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "settings.ini")

	cfg, err := Load(path, nil)
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}
	cfg.Settings.Font = "Lucida Console"
	cfg.Settings.FontSize = 16

	if err := cfg.Save(); err != nil {
		t.Fatalf("Save failed: %v", err)
	}

	cfg2, err := Load(path, nil)
	if err != nil {
		t.Fatalf("second Load failed: %v", err)
	}
	if cfg2.Settings.Font != "Lucida Console" {
		t.Errorf("expected Lucida Console, got %s", cfg2.Settings.Font)
	}
	if cfg2.Settings.FontSize != 16 {
		t.Errorf("expected 16, got %f", cfg2.Settings.FontSize)
	}
}

func TestProfile_SaveAndLoad(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "settings.ini")

	cfg, err := Load(path, nil)
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}

	p := Profile{
		Name:     "my-server",
		Host:     "192.168.1.1",
		Port:     22,
		Username: "admin",
		AuthType: AuthPassword,
		Password: "encrypted-placeholder",
	}
	cfg.SaveProfile(p)

	if err := cfg.Save(); err != nil {
		t.Fatalf("Save failed: %v", err)
	}

	cfg2, err := Load(path, nil)
	if err != nil {
		t.Fatalf("reload failed: %v", err)
	}
	profiles := cfg2.Profiles()
	if len(profiles) != 1 {
		t.Fatalf("expected 1 profile, got %d", len(profiles))
	}
	got := profiles[0]
	if got.Name != "my-server" {
		t.Errorf("expected my-server, got %s", got.Name)
	}
	if got.Host != "192.168.1.1" {
		t.Errorf("expected 192.168.1.1, got %s", got.Host)
	}
	if got.Port != 22 {
		t.Errorf("expected port 22, got %d", got.Port)
	}
	if got.AuthType != AuthPassword {
		t.Errorf("expected password auth, got %s", got.AuthType)
	}
}

func TestProfile_DeleteProfile(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "settings.ini")

	cfg, _ := Load(path, nil)
	cfg.SaveProfile(Profile{Name: "server-a", Host: "1.1.1.1", Port: 22, Username: "user", AuthType: AuthPassword})
	cfg.SaveProfile(Profile{Name: "server-b", Host: "2.2.2.2", Port: 22, Username: "user", AuthType: AuthPassword})
	cfg.Save()

	cfg, _ = Load(path, nil)
	cfg.DeleteProfile("server-a")
	cfg.Save()

	cfg, _ = Load(path, nil)
	profiles := cfg.Profiles()
	if len(profiles) != 1 {
		t.Fatalf("expected 1 profile after delete, got %d", len(profiles))
	}
	if profiles[0].Name != "server-b" {
		t.Errorf("expected server-b, got %s", profiles[0].Name)
	}
}

func TestProfile_EncryptedPasswordRoundTrip(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "settings.ini")

	key := crypto.DeriveKey("test-key-seed")

	cfg, err := Load(path, key)
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}

	p := Profile{
		Name:     "secure-server",
		Host:     "10.0.0.1",
		Port:     22,
		Username: "admin",
		AuthType: AuthPassword,
		Password: "supersecret",
	}
	cfg.SaveProfile(p)
	if err := cfg.Save(); err != nil {
		t.Fatalf("Save failed: %v", err)
	}

	// Verify the password is NOT stored as plaintext.
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("reading INI file: %v", err)
	}
	if strings.Contains(string(raw), "supersecret") {
		t.Error("password stored as plaintext in INI file; expected ciphertext")
	}

	// Reload with the same key and verify the password decrypts correctly.
	cfg2, err := Load(path, key)
	if err != nil {
		t.Fatalf("reload failed: %v", err)
	}
	profiles := cfg2.Profiles()
	if len(profiles) != 1 {
		t.Fatalf("expected 1 profile, got %d", len(profiles))
	}
	if profiles[0].Password != "supersecret" {
		t.Errorf("expected decrypted password 'supersecret', got %q", profiles[0].Password)
	}
}
