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
	file    *os.File
	enabled bool
}

// NewSessionLogger creates a SessionLogger. If enabled is false, all operations are no-ops.
func NewSessionLogger(enabled bool, logDir, logFormat, host string) (*SessionLogger, error) {
	if !enabled {
		return &SessionLogger{enabled: false}, nil
	}
	if err := os.MkdirAll(logDir, 0755); err != nil {
		return nil, fmt.Errorf("creating log directory: %w", err)
	}
	filename := expandLogFormat(logFormat, host, time.Now())
	path := filepath.Join(logDir, filename)
	f, err := os.OpenFile(path, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		return nil, fmt.Errorf("opening log file: %w", err)
	}
	return &SessionLogger{file: f, enabled: true}, nil
}

// WrapReader wraps r so that all bytes read are also tee'd (ANSI-stripped) to the log file.
// Safe to call on a nil receiver; returns r unchanged if logging is disabled.
func (l *SessionLogger) WrapReader(r io.Reader) io.Reader {
	if l == nil || !l.enabled || l.file == nil {
		return r
	}
	return io.TeeReader(r, &ansiStripper{w: l.file})
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
func expandLogFormat(format, host string, t time.Time) string {
	r := strings.NewReplacer(
		"{date}", t.Format("2006-01-02"),
		"{time}", t.Format("15-04-05"),
		"{host}", sanitiseFilename(host),
	)
	return r.Replace(format)
}

// sanitiseFilename replaces characters unsafe in filenames with underscores.
func sanitiseFilename(s string) string {
	return strings.NewReplacer(
		":", "_", "/", "_", "\\", "_", "*", "_",
		"?", "_", "\"", "_", "<", "_", ">", "_", "|", "_",
	).Replace(s)
}

// ansiEscapeRe matches ANSI/VT escape sequences and bare carriage returns.
var ansiEscapeRe = regexp.MustCompile(`\x1b(?:\[[0-9;?]*[A-Za-z]|\][^\a]*\a|[^[]?)|\r`)

// ansiStripper is an io.Writer that strips ANSI escape sequences before writing.
type ansiStripper struct {
	w io.Writer
}

func (a *ansiStripper) Write(p []byte) (int, error) {
	cleaned := ansiEscapeRe.ReplaceAll(p, nil)
	if len(cleaned) > 0 {
		if _, err := a.w.Write(cleaned); err != nil {
			return 0, err
		}
	}
	// Return original length so TeeReader doesn't see a short write.
	return len(p), nil
}
