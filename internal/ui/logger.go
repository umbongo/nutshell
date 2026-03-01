package ui

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"
)

// SessionLogger writes terminal output to a log file, stripping ANSI escape sequences.
type SessionLogger struct {
	file      *os.File
	enabled   bool
	logDir    string
	logFormat string
	host      string
	stripper  *ansiStripper
}

// NewSessionLogger creates a SessionLogger. If enabled is false, all operations are no-ops.
func NewSessionLogger(enabled bool, logDir, logFormat, host string) (*SessionLogger, error) {
	l := &SessionLogger{
		enabled:   false, // Start() will set this to true if enabled
		logDir:    logDir,
		logFormat: logFormat,
		host:      host,
		stripper:  &ansiStripper{},
	}
	if enabled {
		if err := l.Start(); err != nil {
			return nil, err
		}
	}
	return l, nil
}

// IsEnabled returns true if logging is currently active.
func (l *SessionLogger) IsEnabled() bool {
	if l == nil {
		return false
	}
	return l.enabled
}

// Start opens the log file and starts writing to it.
func (l *SessionLogger) Start() error {
	if l.enabled {
		return nil
	}
	if err := os.MkdirAll(l.logDir, 0755); err != nil {
		return fmt.Errorf("creating log directory: %w", err)
	}
	filename := l.expandLogFormat(time.Now())
	path := filepath.Join(l.logDir, filename)
	f, err := os.OpenFile(path, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		return fmt.Errorf("opening log file: %w", err)
	}
	l.file = f
	l.stripper.setWriter(f)
	l.enabled = true
	return nil
}

// Stop closes the log file and stops writing.
func (l *SessionLogger) Stop() {
	if !l.enabled {
		return
	}
	l.stripper.setWriter(nil)
	if l.file != nil {
		_ = l.file.Close()
		l.file = nil
	}
	l.enabled = false
}

// WrapReader wraps r so that all bytes read are also tee'd (ANSI-stripped) to the log file.
// Safe to call on a nil receiver; returns r unchanged if logging is disabled.
func (l *SessionLogger) WrapReader(r io.Reader) io.Reader {
	if l == nil {
		return r
	}
	return io.TeeReader(r, l.stripper)
}

// Close closes the underlying log file.
func (l *SessionLogger) Close() {
	if l == nil {
		return
	}
	if l.file != nil {
		_ = l.file.Close()
		l.file = nil
	}
}

// expandLogFormat replaces {date}, {time}, {host} placeholders with actual values.
func (l *SessionLogger) expandLogFormat(t time.Time) string {
	r := strings.NewReplacer(
		"{date}", t.Format("2006-01-02"),
		"{time}", t.Format("15-04-05"),
		"{host}", l.sanitiseFilename(),
	)
	return r.Replace(l.logFormat)
}

// sanitiseFilename replaces characters unsafe in filenames with underscores.
func (l *SessionLogger) sanitiseFilename() string {
	return strings.NewReplacer(
		":", "_", "/", "_", "\\", "_", "*", "_",
		"?", "_", "\"", "_", "<", "_", ">", "_", "|", "_",
	).Replace(l.host)
}

// ansiEscapeRe matches ANSI/VT escape sequences and bare carriage returns.
var ansiEscapeRe = regexp.MustCompile(`\x1b(?:\[[0-9;?]*[A-Za-z]|\][^\a]*\a|[^[]?)|\r`)

// ansiStripper is an io.Writer that strips ANSI escape sequences before writing.
type ansiStripper struct {
	w io.Writer
}

func (a *ansiStripper) Write(p []byte) (int, error) {
	if a.w == nil {
		return len(p), nil
	}
	cleaned := ansiEscapeRe.ReplaceAll(p, nil)
	if len(cleaned) > 0 {
		if _, err := a.w.Write(cleaned); err != nil {
			return 0, err
		}
	}
	// Return original length so TeeReader doesn't see a short write.
	return len(p), nil
}

func (a *ansiStripper) setWriter(w io.Writer) {
	a.w = w
}
