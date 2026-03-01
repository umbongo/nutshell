package ssh

import (
	"bufio"
	"errors"
	"fmt"
	"net"
	"os"
	"strings"

	gossh "golang.org/x/crypto/ssh"
	"golang.org/x/crypto/ssh/knownhosts"
)

// TOFUCallback creates an ssh.HostKeyCallback that implements trust-on-first-use.
//
// knownHostsPath is the path to the known_hosts file; it is created empty if it
// does not yet exist.  onChanged is called synchronously (on the caller's goroutine)
// when a stored host key has changed; it should return true to accept the new key
// and update the file, or false to reject the connection.
func TOFUCallback(knownHostsPath string, onChanged func(host string, old, new gossh.PublicKey) bool) (gossh.HostKeyCallback, error) {
	if err := ensureKnownHostsFile(knownHostsPath); err != nil {
		return nil, fmt.Errorf("known_hosts: %w", err)
	}

	return func(hostname string, remote net.Addr, key gossh.PublicKey) error {
		cb, err := knownhosts.New(knownHostsPath)
		if err != nil {
			return fmt.Errorf("reading known_hosts: %w", err)
		}

		hostErr := cb(hostname, remote, key)
		if hostErr == nil {
			return nil // key is known and matches
		}

		var ke *knownhosts.KeyError
		if !errors.As(hostErr, &ke) {
			return hostErr
		}

		if len(ke.Want) == 0 {
			// Unknown host — TOFU: accept and persist.
			return appendKnownHost(knownHostsPath, hostname, key)
		}

		// Host key has changed — ask the caller.
		if onChanged != nil && onChanged(hostname, ke.Want[0].Key, key) {
			return replaceKnownHost(knownHostsPath, hostname, key)
		}
		return hostErr // reject
	}, nil
}

func ensureKnownHostsFile(path string) error {
	if _, err := os.Stat(path); os.IsNotExist(err) {
		return os.WriteFile(path, nil, 0600)
	}
	return nil
}

func appendKnownHost(path, hostname string, key gossh.PublicKey) error {
	f, err := os.OpenFile(path, os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0600)
	if err != nil {
		return fmt.Errorf("writing known_hosts: %w", err)
	}
	defer f.Close()
	_, err = fmt.Fprintln(f, knownhosts.Line([]string{hostname}, key))
	return err
}

// replaceKnownHost removes all existing entries for hostname and appends a new one.
func replaceKnownHost(path, hostname string, key gossh.PublicKey) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("reading known_hosts: %w", err)
	}

	norm := knownhosts.Normalize(hostname)
	var kept []string
	scanner := bufio.NewScanner(strings.NewReader(string(data)))
	for scanner.Scan() {
		line := scanner.Text()
		trimmed := strings.TrimSpace(line)
		if trimmed == "" || strings.HasPrefix(trimmed, "#") {
			kept = append(kept, line)
			continue
		}
		fields := strings.Fields(trimmed)
		if len(fields) < 3 {
			kept = append(kept, line)
			continue
		}
		if !hostnameInField(norm, fields[0]) {
			kept = append(kept, line)
		}
	}

	kept = append(kept, knownhosts.Line([]string{hostname}, key))
	content := strings.Join(kept, "\n") + "\n"
	return os.WriteFile(path, []byte(content), 0600)
}

// hostnameInField reports whether norm appears in the comma-separated host field
// of a known_hosts line.
func hostnameInField(norm, field string) bool {
	for _, h := range strings.Split(field, ",") {
		if h == norm {
			return true
		}
	}
	return false
}
