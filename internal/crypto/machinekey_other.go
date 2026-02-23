//go:build !windows

package crypto

import (
	"os"
)

// MachineKey returns a deterministic 32-byte key. On non-Windows platforms
// this uses the hostname as a seed, primarily for development/testing.
func MachineKey() []byte {
	hostname, err := os.Hostname()
	if err != nil {
		hostname = "conga-ssh-dev"
	}
	return DeriveKey(hostname)
}
