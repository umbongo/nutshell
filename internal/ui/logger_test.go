package ui

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestExpandLogFormat_AllPlaceholders(t *testing.T) {
	ts := time.Date(2024, 3, 15, 9, 5, 7, 0, time.UTC)
	got := expandLogFormat("{date}_{time}_{host}.log", "myserver", ts)
	want := "2024-03-15_09-05-07_myserver.log"
	if got != want {
		t.Errorf("expandLogFormat = %q, want %q", got, want)
	}
}

func TestExpandLogFormat_HostSanitised(t *testing.T) {
	ts := time.Now()
	got := expandLogFormat("{host}.log", "10.0.0.1:22", ts)
	if strings.Contains(got, ":") {
		t.Errorf("expandLogFormat did not sanitise colon in host: %q", got)
	}
}

func TestSessionLogger_WritesOutput(t *testing.T) {
	dir := t.TempDir()
	l, err := NewSessionLogger(true, dir, "{host}.log", "testhost")
	if err != nil {
		t.Fatalf("NewSessionLogger: %v", err)
	}
	defer l.Close()

	r := strings.NewReader("hello world\n")
	wrapped := l.WrapReader(r)

	buf := new(bytes.Buffer)
	buf.ReadFrom(wrapped)

	// Data should have been read correctly
	if buf.String() != "hello world\n" {
		t.Errorf("wrapped reader returned %q", buf.String())
	}

	l.Close()

	// Check the log file exists and contains the content
	entries, _ := os.ReadDir(dir)
	if len(entries) == 0 {
		t.Fatal("no log file created")
	}
	content, _ := os.ReadFile(filepath.Join(dir, entries[0].Name()))
	if !strings.Contains(string(content), "hello world") {
		t.Errorf("log file missing expected content, got: %q", string(content))
	}
}

func TestSessionLogger_Disabled_NoOutput(t *testing.T) {
	dir := t.TempDir()
	l, err := NewSessionLogger(false, dir, "{host}.log", "testhost")
	if err != nil {
		t.Fatalf("NewSessionLogger: %v", err)
	}

	r := strings.NewReader("no log please")
	wrapped := l.WrapReader(r)

	buf := new(bytes.Buffer)
	buf.ReadFrom(wrapped)

	entries, _ := os.ReadDir(dir)
	if len(entries) != 0 {
		t.Error("log file created when logging is disabled")
	}
	if buf.String() != "no log please" {
		t.Errorf("unexpected content: %q", buf.String())
	}
}

func TestAnsiStripper_RemovesEscapes(t *testing.T) {
	var buf bytes.Buffer
	w := &ansiStripper{w: &buf}

	input := "\x1b[32mGreen text\x1b[0m\n"
	n, err := w.Write([]byte(input))
	if err != nil {
		t.Fatalf("Write error: %v", err)
	}
	if n != len(input) {
		t.Errorf("Write returned %d, want %d", n, len(input))
	}
	got := buf.String()
	if strings.Contains(got, "\x1b") {
		t.Errorf("ANSI escape not stripped: %q", got)
	}
	if !strings.Contains(got, "Green text") {
		t.Errorf("text content missing: %q", got)
	}
}

func TestAnsiStripper_PassesPlainText(t *testing.T) {
	var buf bytes.Buffer
	w := &ansiStripper{w: &buf}

	input := "plain text output\n"
	w.Write([]byte(input))
	if buf.String() != input {
		t.Errorf("plain text modified: got %q, want %q", buf.String(), input)
	}
}

func TestAnsiStripper_StripsCarriageReturn(t *testing.T) {
	var buf bytes.Buffer
	w := &ansiStripper{w: &buf}
	w.Write([]byte("line1\r\nline2\r\n"))
	got := buf.String()
	if strings.Contains(got, "\r") {
		t.Errorf("carriage return not stripped: %q", got)
	}
}
