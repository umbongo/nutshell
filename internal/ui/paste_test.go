package ui

import (
	"bytes"
	"io"
	"strings"
	"testing"
	"time"
)

// nopCloser wraps an io.Writer with a no-op Close method.
type nopCloser struct{ w io.Writer }

func (n *nopCloser) Write(p []byte) (int, error) { return n.w.Write(p) }
func (n *nopCloser) Close() error                { return nil }

func TestSendWithDelay_SingleLine(t *testing.T) {
	var buf bytes.Buffer
	err := sendWithDelay(&buf, "hello world", 0)
	if err != nil {
		t.Fatalf("sendWithDelay error: %v", err)
	}
	if buf.String() != "hello world" {
		t.Errorf("got %q, want %q", buf.String(), "hello world")
	}
}

func TestSendWithDelay_MultiLine(t *testing.T) {
	var buf bytes.Buffer
	start := time.Now()
	err := sendWithDelay(&buf, "line1\nline2\nline3", 10)
	elapsed := time.Since(start)
	if err != nil {
		t.Fatalf("sendWithDelay error: %v", err)
	}
	got := buf.String()
	if !strings.Contains(got, "line1") || !strings.Contains(got, "line2") || !strings.Contains(got, "line3") {
		t.Errorf("missing lines in output: %q", got)
	}
	// Should have slept at least 2 * 10ms (between 3 lines)
	if elapsed < 15*time.Millisecond {
		t.Errorf("expected at least 15ms delay for 3 lines, got %v", elapsed)
	}
}

func TestSendWithDelay_NoDelayWhenZero(t *testing.T) {
	var buf bytes.Buffer
	start := time.Now()
	err := sendWithDelay(&buf, "a\nb\nc\nd\ne", 0)
	elapsed := time.Since(start)
	if err != nil {
		t.Fatalf("sendWithDelay error: %v", err)
	}
	if elapsed > 50*time.Millisecond {
		t.Errorf("expected near-zero delay with delayMs=0, got %v", elapsed)
	}
}

func TestLooksLikePaste_PrintableContent(t *testing.T) {
	if !looksLikePaste([]byte("hello world this is text")) {
		t.Error("expected printable text to look like paste")
	}
}

func TestLooksLikePaste_BinaryContent(t *testing.T) {
	binary := []byte{0x00, 0x01, 0x02, 0x03, 0x04, 0x05}
	if looksLikePaste(binary) {
		t.Error("expected binary content to NOT look like paste")
	}
}

func TestPasteInterceptor_SingleByte_PassThrough(t *testing.T) {
	var buf bytes.Buffer
	inner := &nopCloser{w: &buf}
	p := NewPasteInterceptor(inner, nil, 0)

	// Single byte (keystroke) must pass through without showing a dialog.
	n, err := p.Write([]byte("x"))
	if err != nil {
		t.Fatalf("Write error: %v", err)
	}
	if n != 1 {
		t.Errorf("Write returned %d, want 1", n)
	}
	if buf.String() != "x" {
		t.Errorf("got %q, want %q", buf.String(), "x")
	}
}

func TestPasteInterceptor_EscapeSequence_PassThrough(t *testing.T) {
	var buf bytes.Buffer
	inner := &nopCloser{w: &buf}
	p := NewPasteInterceptor(inner, nil, 0)

	esc := []byte("\x1b[A") // cursor up
	n, err := p.Write(esc)
	if err != nil {
		t.Fatalf("Write error: %v", err)
	}
	if n != len(esc) {
		t.Errorf("Write returned %d, want %d", n, len(esc))
	}
	if !bytes.Equal(buf.Bytes(), esc) {
		t.Errorf("escape sequence modified: %q", buf.Bytes())
	}
}
