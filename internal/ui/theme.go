package ui

import (
	"fmt"
	"image/color"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"

	"conga.ssh/internal/config"
	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/theme"
)

// terminalTheme is a custom Fyne theme that controls terminal font, size, and colours.
type terminalTheme struct {
	fyne.Theme

	mu       sync.RWMutex
	fontSize float32
	fgColor  color.Color
	bgColor  color.Color
	fontRes  fyne.Resource
}

// NewTerminalTheme creates a terminalTheme from the given config settings.
func NewTerminalTheme(cfg *config.Config) *terminalTheme {
	t := &terminalTheme{
		fontSize: float32(cfg.Settings.FontSize),
	}
	if c, err := parseHexColour(cfg.Settings.ForegroundColour); err == nil {
		t.fgColor = c
	} else {
		t.fgColor = color.NRGBA{R: 0x0C, G: 0x0C, B: 0x0C, A: 0xFF}
	}
	if c, err := parseHexColour(cfg.Settings.BackgroundColour); err == nil {
		t.bgColor = c
	} else {
		t.bgColor = color.NRGBA{R: 0xF2, G: 0xF2, B: 0xF2, A: 0xFF}
	}
	t.Theme = pickBaseTheme(t.bgColor)
	t.fontRes = loadSystemFont(cfg.Settings.Font)
	return t
}

// pickBaseTheme returns LightTheme for light backgrounds, DarkTheme for dark ones.
// This ensures all non-terminal UI elements (dialogs, inputs, buttons) match.
func pickBaseTheme(bg color.Color) fyne.Theme {
	r, g, b, _ := bg.RGBA()
	luminance := 0.299*float64(r>>8) + 0.587*float64(g>>8) + 0.114*float64(b>>8)
	if luminance > 128 {
		return theme.LightTheme()
	}
	return theme.DarkTheme()
}

func (t *terminalTheme) Color(n fyne.ThemeColorName, v fyne.ThemeVariant) color.Color {
	t.mu.RLock()
	defer t.mu.RUnlock()
	switch n {
	case theme.ColorNameForeground:
		return t.fgColor
	case theme.ColorNameBackground:
		return t.bgColor
	}
	// Delegate to the base theme using its natural variant so the rest of the
	// UI (dialogs, inputs, buttons) is consistently light or dark.
	return t.Theme.Color(n, v)
}

func (t *terminalTheme) Size(n fyne.ThemeSizeName) float32 {
	t.mu.RLock()
	defer t.mu.RUnlock()
	if n == theme.SizeNameText {
		return t.fontSize
	}
	return t.Theme.Size(n)
}

func (t *terminalTheme) Font(style fyne.TextStyle) fyne.Resource {
	t.mu.RLock()
	defer t.mu.RUnlock()
	if style.Monospace && t.fontRes != nil {
		return t.fontRes
	}
	return t.Theme.Font(style)
}

// FontSize returns the current font size (safe for concurrent access).
func (t *terminalTheme) FontSize() float32 {
	t.mu.RLock()
	defer t.mu.RUnlock()
	return t.fontSize
}

// AdjustFontSize changes the font size by delta, clamped to [6, 72].
func (t *terminalTheme) AdjustFontSize(delta float32) {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.fontSize += delta
	if t.fontSize < 6 {
		t.fontSize = 6
	}
	if t.fontSize > 72 {
		t.fontSize = 72
	}
}

// Apply updates all theme fields from config settings.
func (t *terminalTheme) Apply(cfg *config.Config) {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.fontSize = float32(cfg.Settings.FontSize)
	if c, err := parseHexColour(cfg.Settings.ForegroundColour); err == nil {
		t.fgColor = c
	}
	if c, err := parseHexColour(cfg.Settings.BackgroundColour); err == nil {
		t.bgColor = c
		t.Theme = pickBaseTheme(t.bgColor)
	}
	t.fontRes = loadSystemFont(cfg.Settings.Font)
}

// parseHexColour converts a "#RRGGBB" hex string to color.NRGBA.
func parseHexColour(hex string) (color.NRGBA, error) {
	hex = strings.TrimPrefix(hex, "#")
	if len(hex) != 6 {
		return color.NRGBA{}, fmt.Errorf("invalid hex colour: %q", hex)
	}
	r, err := strconv.ParseUint(hex[0:2], 16, 8)
	if err != nil {
		return color.NRGBA{}, err
	}
	g, err := strconv.ParseUint(hex[2:4], 16, 8)
	if err != nil {
		return color.NRGBA{}, err
	}
	b, err := strconv.ParseUint(hex[4:6], 16, 8)
	if err != nil {
		return color.NRGBA{}, err
	}
	return color.NRGBA{R: uint8(r), G: uint8(g), B: uint8(b), A: 0xFF}, nil
}

// colourToHex converts a color.Color to a "#RRGGBB" hex string.
func colourToHex(c color.Color) string {
	r, g, b, _ := c.RGBA()
	return fmt.Sprintf("#%02X%02X%02X", r>>8, g>>8, b>>8)
}

// loadSystemFont loads a named font from C:\Windows\Fonts\.
// Returns nil if not found; Fyne will fall back to its built-in monospace font.
func loadSystemFont(name string) fyne.Resource {
	fontFiles := map[string]string{
		"Consolas":       "consola.ttf",
		"Courier New":    "cour.ttf",
		"Lucida Console": "lucon.ttf",
		"Cascadia Code":  "CascadiaCode.ttf",
		"Cascadia Mono":  "CascadiaMono.ttf",
	}
	filename, ok := fontFiles[name]
	if !ok {
		return nil
	}
	path := filepath.Join(`C:\Windows\Fonts`, filename)
	data, err := os.ReadFile(path)
	if err != nil {
		return nil
	}
	return fyne.NewStaticResource(filename, data)
}
