package crypto

import (
	"testing"
)

func TestEncryptDecrypt_RoundTrip(t *testing.T) {
	key := make([]byte, 32)
	copy(key, []byte("test-machine-key-padded-to-32b!!"))

	plain := "s3cr3tP@ssword!"
	encrypted, err := Encrypt(plain, key)
	if err != nil {
		t.Fatalf("Encrypt failed: %v", err)
	}
	if encrypted == plain {
		t.Error("encrypted text should not equal plaintext")
	}
	if encrypted == "" {
		t.Error("encrypted text should not be empty")
	}

	decrypted, err := Decrypt(encrypted, key)
	if err != nil {
		t.Fatalf("Decrypt failed: %v", err)
	}
	if decrypted != plain {
		t.Errorf("expected %q, got %q", plain, decrypted)
	}
}

func TestEncrypt_UniqueEachTime(t *testing.T) {
	key := make([]byte, 32)
	copy(key, []byte("test-machine-key-padded-to-32b!!"))

	plain := "same-password"
	enc1, _ := Encrypt(plain, key)
	enc2, _ := Encrypt(plain, key)

	// Random IV means ciphertexts should differ
	if enc1 == enc2 {
		t.Error("two encryptions of the same plaintext should produce different ciphertexts (random IV)")
	}
}

func TestDecrypt_WrongKey(t *testing.T) {
	key1 := make([]byte, 32)
	copy(key1, []byte("correct-key-padded-to-32-bytes!!"))
	key2 := make([]byte, 32)
	copy(key2, []byte("wrong-key-padded-to-32-bytes!!!!"))

	encrypted, err := Encrypt("secret", key1)
	if err != nil {
		t.Fatal(err)
	}

	_, err = Decrypt(encrypted, key2)
	if err == nil {
		t.Error("expected error decrypting with wrong key")
	}
}

func TestEncrypt_EmptyString(t *testing.T) {
	key := make([]byte, 32)
	copy(key, []byte("test-machine-key-padded-to-32b!!"))

	encrypted, err := Encrypt("", key)
	if err != nil {
		t.Fatalf("Encrypt empty string failed: %v", err)
	}
	decrypted, err := Decrypt(encrypted, key)
	if err != nil {
		t.Fatalf("Decrypt empty string failed: %v", err)
	}
	if decrypted != "" {
		t.Errorf("expected empty string, got %q", decrypted)
	}
}

func TestDeriveKey_Deterministic(t *testing.T) {
	// Same seed should always produce the same 32-byte key
	seed := "test-machine-identifier"
	k1 := DeriveKey(seed)
	k2 := DeriveKey(seed)
	if string(k1) != string(k2) {
		t.Error("DeriveKey should be deterministic for the same seed")
	}
	if len(k1) != 32 {
		t.Errorf("expected 32-byte key, got %d", len(k1))
	}
}

func TestDeriveKey_DifferentSeeds(t *testing.T) {
	k1 := DeriveKey("machine-a")
	k2 := DeriveKey("machine-b")
	if string(k1) == string(k2) {
		t.Error("different seeds should produce different keys")
	}
}
