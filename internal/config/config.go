package config

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"conga.ssh/internal/crypto"
	"gopkg.in/ini.v1"
)

var loadOpts = ini.LoadOptions{
	IgnoreInlineComment: true,
}

// AuthType defines the SSH authentication method for a profile.
type AuthType string

const (
	AuthPassword AuthType = "password"
	AuthKey      AuthType = "key"
)

// Settings holds all global application settings.
type Settings struct {
	Font                string  `ini:"font"`
	FontSize            float64 `ini:"font_size"`
	ScrollbackLines     int     `ini:"scrollback_lines"`
	PasteDelayMs        int     `ini:"paste_delay_ms"`
	LoggingEnabled      bool    `ini:"logging_enabled"`
	LogFormat           string  `ini:"log_format"`
	LogDir              string  `ini:"log_dir"`
	HostKeyVerification bool    `ini:"host_key_verification"`
	ForegroundColour    string  `ini:"foreground_colour"`
	BackgroundColour    string  `ini:"background_colour"`
}

// Profile represents a saved SSH connection profile.
type Profile struct {
	Name     string
	Host     string
	Port     int
	Username string
	AuthType AuthType
	Password string // stored encrypted
	KeyPath  string // path to private key file
}

// Config holds the full application configuration.
type Config struct {
	Settings       Settings
	KnownHostsPath string // path to the known_hosts file; derived from Settings path at load time
	path           string
	file           *ini.File
	key            []byte // AES-GCM key for encrypting passwords; nil means no encryption
}

func newSettings() Settings {
	return Settings{
		Font:                "Consolas",
		FontSize:            12,
		ScrollbackLines:     3000,
		PasteDelayMs:        350,
		LoggingEnabled:      false,
		LogFormat:           "{date}_{time}_{host}.log",
		LogDir:              ".",
		HostKeyVerification: false,
		ForegroundColour:    "#0C0C0C",
		BackgroundColour:    "#F2F2F2",
	}
}

// Load reads settings.ini from path, creating it with defaults if it does not exist.
// key is the AES-GCM encryption key used for profile passwords; pass nil to disable encryption.
func Load(path string, key []byte) (*Config, error) {
	cfg := &Config{
		Settings:       newSettings(),
		KnownHostsPath: filepath.Join(filepath.Dir(path), "known_hosts"),
		path:           path,
		key:            key,
	}

	if _, err := os.Stat(path); os.IsNotExist(err) {
		f := ini.Empty()
		cfg.file = f
		if err := cfg.writeDefaults(); err != nil {
			return nil, fmt.Errorf("creating settings.ini: %w", err)
		}
		return cfg, nil
	}

	f, err := ini.LoadSources(loadOpts, path)
	if err != nil {
		return nil, fmt.Errorf("loading settings.ini: %w", err)
	}

	cfg.file = f
	if sec, err := f.GetSection("settings"); err == nil {
		if err := sec.MapTo(&cfg.Settings); err != nil {
			return nil, fmt.Errorf("mapping settings: %w", err)
		}
	}

	return cfg, nil
}

// Save persists the current settings and profiles to disk.
func (c *Config) Save() error {
	sec, err := c.file.GetSection("settings")
	if err != nil {
		sec, err = c.file.NewSection("settings")
		if err != nil {
			return err
		}
	}
	if err := sec.ReflectFrom(&c.Settings); err != nil {
		return err
	}
	return c.file.SaveTo(c.path)
}

// Profiles returns all saved connection profiles.
func (c *Config) Profiles() []Profile {
	var profiles []Profile
	for _, sec := range c.file.Sections() {
		if !strings.HasPrefix(sec.Name(), "profile:") {
			continue
		}
		name := strings.TrimPrefix(sec.Name(), "profile:")
		storedPassword := sec.Key("password").String()
		password := storedPassword
		if c.key != nil && storedPassword != "" {
			if dec, err := crypto.Decrypt(storedPassword, c.key); err == nil {
				password = dec
			}
			// If decrypt fails the value is likely plaintext (migration); use as-is.
		}

		p := Profile{
			Name:     name,
			Host:     sec.Key("host").String(),
			Port:     sec.Key("port").MustInt(22),
			Username: sec.Key("username").String(),
			AuthType: AuthType(sec.Key("auth_type").MustString(string(AuthPassword))),
			Password: password,
			KeyPath:  sec.Key("key_path").String(),
		}
		profiles = append(profiles, p)
	}
	return profiles
}

// SaveProfile adds or updates a profile in the config. Call Save() to persist.
// Passwords are encrypted with the machine key if one was provided at Load time.
func (c *Config) SaveProfile(p Profile) {
	storedPassword := p.Password
	if c.key != nil && p.Password != "" {
		if enc, err := crypto.Encrypt(p.Password, c.key); err == nil {
			storedPassword = enc
		}
	}

	secName := "profile:" + p.Name
	c.file.DeleteSection(secName)
	sec, _ := c.file.NewSection(secName)
	sec.Key("host").SetValue(p.Host)
	sec.Key("port").SetValue(fmt.Sprintf("%d", p.Port))
	sec.Key("username").SetValue(p.Username)
	sec.Key("auth_type").SetValue(string(p.AuthType))
	sec.Key("password").SetValue(storedPassword)
	sec.Key("key_path").SetValue(p.KeyPath)
}

// DeleteProfile removes a profile by name. Call Save() to persist.
func (c *Config) DeleteProfile(name string) {
	c.file.DeleteSection("profile:" + name)
}

func (c *Config) writeDefaults() error {
	sec, err := c.file.NewSection("settings")
	if err != nil {
		return err
	}
	if err := sec.ReflectFrom(&c.Settings); err != nil {
		return err
	}
	return c.file.SaveTo(c.path)
}
