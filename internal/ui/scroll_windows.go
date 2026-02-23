//go:build windows

package ui

import "syscall"

var (
	user32DLL            = syscall.NewLazyDLL("user32.dll")
	procGetAsyncKeyState = user32DLL.NewProc("GetAsyncKeyState")
)

// isCtrlDown returns true if either Ctrl key is currently held down.
func isCtrlDown() bool {
	const VK_CONTROL = 0x11
	ret, _, _ := procGetAsyncKeyState.Call(uintptr(VK_CONTROL))
	return ret&0x8000 != 0
}
