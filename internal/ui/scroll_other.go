//go:build !windows

package ui

// isCtrlDown always returns false on non-Windows platforms.
func isCtrlDown() bool { return false }
