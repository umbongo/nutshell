package ssh

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"net"
	"os"
	"path/filepath"
	"testing"

	gossh "golang.org/x/crypto/ssh"
)

func generateTestPublicKey(t *testing.T) gossh.PublicKey {
	t.Helper()
	priv, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	pub, err := gossh.NewPublicKey(&priv.PublicKey)
	if err != nil {
		t.Fatal(err)
	}
	return pub
}

func fakeAddr(s string) net.Addr {
	addr, _ := net.ResolveTCPAddr("tcp", s)
	return addr
}

func TestTOFU_CreatesFileIfAbsent(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "known_hosts")

	_, err := TOFUCallback(path, nil)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(path); err != nil {
		t.Fatalf("expected known_hosts to be created: %v", err)
	}
}

func TestTOFU_NewHost(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "known_hosts")
	key := generateTestPublicKey(t)

	cb, err := TOFUCallback(path, nil)
	if err != nil {
		t.Fatal(err)
	}
	if err := cb("example.com:22", fakeAddr("example.com:22"), key); err != nil {
		t.Fatalf("expected new host to be accepted: %v", err)
	}
	data, _ := os.ReadFile(path)
	if len(data) == 0 {
		t.Fatal("expected known_hosts to be non-empty after first connection")
	}
}

func TestTOFU_KnownHostSameKey(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "known_hosts")
	key := generateTestPublicKey(t)

	cb, err := TOFUCallback(path, nil)
	if err != nil {
		t.Fatal(err)
	}
	if err := cb("example.com:22", fakeAddr("example.com:22"), key); err != nil {
		t.Fatal(err)
	}

	// Second connection with same key should pass without callback.
	cb2, err := TOFUCallback(path, func(host string, old, new gossh.PublicKey) bool {
		t.Fatal("onChanged should not be called for same key")
		return false
	})
	if err != nil {
		t.Fatal(err)
	}
	if err := cb2("example.com:22", fakeAddr("example.com:22"), key); err != nil {
		t.Fatalf("expected same key to be accepted: %v", err)
	}
}

func TestTOFU_ChangedKey_Accept(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "known_hosts")
	key1 := generateTestPublicKey(t)
	key2 := generateTestPublicKey(t)

	cb, err := TOFUCallback(path, nil)
	if err != nil {
		t.Fatal(err)
	}
	if err := cb("example.com:22", fakeAddr("example.com:22"), key1); err != nil {
		t.Fatal(err)
	}

	// Second connection with a different key; user accepts.
	cb2, err := TOFUCallback(path, func(host string, old, new gossh.PublicKey) bool {
		return true
	})
	if err != nil {
		t.Fatal(err)
	}
	if err := cb2("example.com:22", fakeAddr("example.com:22"), key2); err != nil {
		t.Fatalf("expected changed key to be accepted: %v", err)
	}

	// Third connection: new key should now pass without any prompt.
	cb3, err := TOFUCallback(path, func(host string, old, new gossh.PublicKey) bool {
		t.Fatal("onChanged should not be called after key update")
		return false
	})
	if err != nil {
		t.Fatal(err)
	}
	if err := cb3("example.com:22", fakeAddr("example.com:22"), key2); err != nil {
		t.Fatalf("expected updated key to pass: %v", err)
	}
}

func TestTOFU_ChangedKey_Reject(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "known_hosts")
	key1 := generateTestPublicKey(t)
	key2 := generateTestPublicKey(t)

	cb, err := TOFUCallback(path, nil)
	if err != nil {
		t.Fatal(err)
	}
	if err := cb("example.com:22", fakeAddr("example.com:22"), key1); err != nil {
		t.Fatal(err)
	}

	// Second connection with different key; user rejects.
	cb2, err := TOFUCallback(path, func(host string, old, new gossh.PublicKey) bool {
		return false
	})
	if err != nil {
		t.Fatal(err)
	}
	if err := cb2("example.com:22", fakeAddr("example.com:22"), key2); err == nil {
		t.Fatal("expected error when user rejects changed key")
	}
}

func TestTOFU_NonStandardPort(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "known_hosts")
	key := generateTestPublicKey(t)

	cb, err := TOFUCallback(path, nil)
	if err != nil {
		t.Fatal(err)
	}
	// Port 2222 should be stored in [host]:2222 format.
	if err := cb("example.com:2222", fakeAddr("example.com:2222"), key); err != nil {
		t.Fatalf("expected new host on port 2222 to be accepted: %v", err)
	}

	cb2, err := TOFUCallback(path, nil)
	if err != nil {
		t.Fatal(err)
	}
	if err := cb2("example.com:2222", fakeAddr("example.com:2222"), key); err != nil {
		t.Fatalf("expected same key on port 2222 to be accepted: %v", err)
	}
}
