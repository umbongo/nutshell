package ui

import (
	"testing"

	"fyne.io/fyne/v2/theme"
)

func TestParseHexColour_Valid(t *testing.T) {
	tests := []struct {
		input string
		r, g, b uint8
	}{
		{"#CCCCCC", 0xCC, 0xCC, 0xCC},
		{"#0C0C0C", 0x0C, 0x0C, 0x0C},
		{"#13A10E", 0x13, 0xA1, 0x0E},
		{"#0037DA", 0x00, 0x37, 0xDA},
		{"000000", 0x00, 0x00, 0x00},  // no hash prefix
		{"FFFFFF", 0xFF, 0xFF, 0xFF},
	}
	for _, tt := range tests {
		c, err := parseHexColour(tt.input)
		if err != nil {
			t.Errorf("parseHexColour(%q): unexpected error: %v", tt.input, err)
			continue
		}
		if c.R != tt.r || c.G != tt.g || c.B != tt.b {
			t.Errorf("parseHexColour(%q) = {%02X,%02X,%02X}, want {%02X,%02X,%02X}",
				tt.input, c.R, c.G, c.B, tt.r, tt.g, tt.b)
		}
		if c.A != 0xFF {
			t.Errorf("parseHexColour(%q): expected alpha 0xFF, got %02X", tt.input, c.A)
		}
	}
}

func TestParseHexColour_Invalid(t *testing.T) {
	invalid := []string{"#ZZZ", "#1234", "short", "", "#GGGGGG"}
	for _, s := range invalid {
		if _, err := parseHexColour(s); err == nil {
			t.Errorf("parseHexColour(%q): expected error, got nil", s)
		}
	}
}

func TestColourToHex(t *testing.T) {
	c, _ := parseHexColour("#3A96DD")
	got := colourToHex(c)
	if got != "#3A96DD" {
		t.Errorf("colourToHex roundtrip: got %q, want #3A96DD", got)
	}
}

func TestTerminalTheme_Size_OverridesText(t *testing.T) {
	th := &terminalTheme{
		Theme:    theme.DarkTheme(),
		fontSize: 14,
	}
	got := th.Size(theme.SizeNameText)
	if got != 14 {
		t.Errorf("Size(SizeNameText) = %v, want 14", got)
	}
}

func TestTerminalTheme_AdjustFontSize_Bounds(t *testing.T) {
	th := &terminalTheme{Theme: theme.DarkTheme(), fontSize: 12}

	// Clamp at minimum
	th.AdjustFontSize(-100)
	if th.fontSize != 6 {
		t.Errorf("expected minimum 6, got %v", th.fontSize)
	}

	// Clamp at maximum
	th.AdjustFontSize(200)
	if th.fontSize != 72 {
		t.Errorf("expected maximum 72, got %v", th.fontSize)
	}

	// Normal adjustment
	th.fontSize = 12
	th.AdjustFontSize(2)
	if th.fontSize != 14 {
		t.Errorf("expected 14 after +2, got %v", th.fontSize)
	}
	th.AdjustFontSize(-3)
	if th.fontSize != 11 {
		t.Errorf("expected 11 after -3, got %v", th.fontSize)
	}
}
