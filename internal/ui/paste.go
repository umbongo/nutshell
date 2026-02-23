package ui

import (
	"fmt"
	"io"
	"strings"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/widget"
)

// PasteInterceptor wraps a WriteCloser to intercept paste operations.
// Short writes (keystrokes, escape sequences) pass through immediately.
// Larger printable writes trigger a confirmation dialog before sending.
type PasteInterceptor struct {
	inner   io.WriteCloser
	window  fyne.Window
	delayMs int
}

// NewPasteInterceptor creates a PasteInterceptor.
func NewPasteInterceptor(inner io.WriteCloser, window fyne.Window, delayMs int) *PasteInterceptor {
	return &PasteInterceptor{inner: inner, window: window, delayMs: delayMs}
}

func (p *PasteInterceptor) Write(data []byte) (int, error) {
	if len(data) == 0 {
		return 0, nil
	}
	// Escape sequences: pass through immediately.
	if data[0] == '\x1b' {
		return p.inner.Write(data)
	}
	// Short writes pass through immediately. This covers all single keystrokes
	// (TypedRune: 1–4 bytes), Enter (\r: 1 byte), Enter in newline mode (\r\n:
	// 2 bytes), and function-key sequences (typically ≤6 bytes).
	if len(data) <= 8 {
		return p.inner.Write(data)
	}
	// Longer non-printable content: pass through.
	if !looksLikePaste(data) {
		return p.inner.Write(data)
	}

	// Paste detected. Show a confirmation dialog asynchronously so we never
	// block the caller's goroutine (the terminal calls Write from the main
	// goroutine via TypedRune/pasteText, so blocking here would deadlock).
	content := string(data)
	inner := p.inner
	delayMs := p.delayMs

	fyne.Do(func() {
		lineCount := strings.Count(content, "\n")
		var msg string
		if lineCount > 0 {
			msg = fmt.Sprintf("Paste %d lines (%d characters)?", lineCount+1, len(content))
		} else {
			msg = fmt.Sprintf("Paste %d characters?", len(content))
		}
		preview := content
		if len(preview) > 300 {
			preview = preview[:300] + "…"
		}
		lbl := widget.NewLabel(msg)
		previewLbl := widget.NewLabel(preview)
		previewLbl.Wrapping = fyne.TextWrapWord

		dialog.ShowCustomConfirm("Confirm Paste", "Paste", "Cancel",
			container.NewVBox(lbl, widget.NewSeparator(), previewLbl),
			func(ok bool) {
				if ok {
					go func() { _ = sendWithDelay(inner, content, delayMs) }()
				}
			}, p.window)
	})

	// Return immediately; actual write happens inside the dialog callback above.
	return len(data), nil
}

func (p *PasteInterceptor) Close() error {
	return p.inner.Close()
}

// looksLikePaste returns true if the byte slice is mostly printable (paste heuristic).
func looksLikePaste(data []byte) bool {
	printable := 0
	for _, b := range data {
		if b >= 0x20 || b == '\n' || b == '\r' || b == '\t' {
			printable++
		}
	}
	return printable*2 >= len(data)
}

// sendWithDelay sends content to w line-by-line with an optional inter-line delay.
func sendWithDelay(w io.Writer, content string, delayMs int) error {
	lines := strings.Split(content, "\n")
	for i, line := range lines {
		toSend := line
		if i < len(lines)-1 {
			toSend += "\n"
		}
		if toSend == "" {
			continue
		}
		if _, err := io.WriteString(w, toSend); err != nil {
			return err
		}
		if i < len(lines)-1 && delayMs > 0 {
			time.Sleep(time.Duration(delayMs) * time.Millisecond)
		}
	}
	return nil
}
