//go:build windows

package crypto

import (
	"golang.org/x/sys/windows/registry"
)

// MachineKey returns a deterministic 32-byte key derived from the Windows
// machine GUID stored in the registry.
func MachineKey() []byte {
	k, err := registry.OpenKey(
		registry.LOCAL_MACHINE,
		`SOFTWARE\Microsoft\Cryptography`,
		registry.QUERY_VALUE,
	)
	if err != nil {
		// Fallback to a fixed seed if registry is unavailable.
		return DeriveKey("conga-ssh-fallback")
	}
	defer k.Close()

	guid, _, err := k.GetStringValue("MachineGuid")
	if err != nil {
		return DeriveKey("conga-ssh-fallback")
	}
	return DeriveKey(guid)
}
