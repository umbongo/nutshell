package ssh

import (
	"fmt"
	"io"
	"net"
	"os"
	"sync"
	"time"

	"golang.org/x/crypto/ssh"
)

// SessionConfig holds the parameters needed to establish an SSH connection.
type SessionConfig struct {
	Host                string
	Port                int
	Username            string
	Password            string
	KeyPath             string // path to private key file; used when AuthType is key
	HostKeyVerification bool
	KnownHostsPath      string                                                         // path to known_hosts file; required when HostKeyVerification is true
	HostKeyChangedFn    func(host string, old, new ssh.PublicKey) bool                 // called when a stored host key has changed
}

// Session represents a single SSH connection and PTY session.
type Session struct {
	mu      sync.Mutex
	config  SessionConfig
	client  *ssh.Client
	session *ssh.Session
	stdin   io.WriteCloser
	stdout  io.Reader
}

// NewSession creates a new Session with the given config. If Port is 0, defaults to 22.
func NewSession(cfg SessionConfig) *Session {
	if cfg.Port == 0 {
		cfg.Port = 22
	}
	return &Session{config: cfg}
}

// Address returns the host:port string for the connection.
func (s *Session) Address() string {
	return fmt.Sprintf("%s:%d", s.config.Host, s.config.Port)
}

// IsConnected reports whether the session has an active SSH connection.
func (s *Session) IsConnected() bool {
	return s.client != nil
}

// Connect establishes the SSH connection and opens a PTY session.
// Returns an error if the connection or authentication fails.
func (s *Session) Connect() error {
	hostKeyCallback := ssh.InsecureIgnoreHostKey()
	if s.config.HostKeyVerification && s.config.KnownHostsPath != "" {
		cb, err := TOFUCallback(s.config.KnownHostsPath, s.config.HostKeyChangedFn)
		if err != nil {
			return fmt.Errorf("host key verification setup: %w", err)
		}
		hostKeyCallback = cb
	}

	authMethods := []ssh.AuthMethod{
		ssh.Password(s.config.Password),
	}

	if s.config.KeyPath != "" {
		keyBytes, err := os.ReadFile(s.config.KeyPath)
		if err != nil {
			return fmt.Errorf("reading key file: %w", err)
		}
		signer, err := ssh.ParsePrivateKey(keyBytes)
		if err != nil {
			// Try with passphrase (Password field is repurposed as passphrase for key auth).
			if s.config.Password != "" {
				signer, err = ssh.ParsePrivateKeyWithPassphrase(keyBytes, []byte(s.config.Password))
			}
			if err != nil {
				return fmt.Errorf("parsing private key: %w", err)
			}
		}
		authMethods = []ssh.AuthMethod{ssh.PublicKeys(signer)}
	}

	clientCfg := &ssh.ClientConfig{
		User:            s.config.Username,
		Auth:            authMethods,
		HostKeyCallback: hostKeyCallback,
		Timeout:         15 * time.Second,
	}

	conn, err := net.DialTimeout("tcp", s.Address(), 15*time.Second)
	if err != nil {
		return fmt.Errorf("connection failed: %w", err)
	}

	sshConn, chans, reqs, err := ssh.NewClientConn(conn, s.Address(), clientCfg)
	if err != nil {
		conn.Close()
		return fmt.Errorf("SSH handshake failed: %w", err)
	}

	s.client = ssh.NewClient(sshConn, chans, reqs)
	return nil
}

// PTYPipes holds the stdin and stdout pipes for a PTY session.
type PTYPipes struct {
	Stdin  io.WriteCloser
	Stdout io.Reader
}

// StartPTY opens a PTY session and returns the stdin/stdout pipes for the terminal.
// Must be called after Connect.
func (s *Session) StartPTY(termType string, width, height int) (*PTYPipes, error) {
	if s.client == nil {
		return nil, fmt.Errorf("not connected")
	}

	sess, err := s.client.NewSession()
	if err != nil {
		return nil, fmt.Errorf("open session: %w", err)
	}
	s.session = sess

	modes := ssh.TerminalModes{
		ssh.ECHO:          1,
		ssh.TTY_OP_ISPEED: 38400,
		ssh.TTY_OP_OSPEED: 38400,
	}
	if err := sess.RequestPty(termType, height, width, modes); err != nil {
		return nil, fmt.Errorf("request PTY: %w", err)
	}

	stdin, err := sess.StdinPipe()
	if err != nil {
		return nil, fmt.Errorf("stdin pipe: %w", err)
	}
	stdout, err := sess.StdoutPipe()
	if err != nil {
		return nil, fmt.Errorf("stdout pipe: %w", err)
	}

	s.stdin = stdin
	s.stdout = stdout

	if err := sess.Shell(); err != nil {
		return nil, fmt.Errorf("start shell: %w", err)
	}

	return &PTYPipes{Stdin: stdin, Stdout: stdout}, nil
}

// ResizePTY sends a window resize signal to the remote PTY.
func (s *Session) ResizePTY(width, height int) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.session == nil {
		return nil
	}
	return s.session.WindowChange(height, width)
}

// Close cleanly terminates the SSH session and connection.
func (s *Session) Close() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.session != nil {
		s.session.Close()
		s.session = nil
	}
	if s.client != nil {
		s.client.Close()
		s.client = nil
	}
}
