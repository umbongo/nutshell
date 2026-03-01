package ui

import (
	"fmt"
	"image/color"
	"io"
	"log"
	"math"
	"time"

	"conga.ssh/internal/config"
	internalssh "conga.ssh/internal/ssh"
	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
	fyneterm "github.com/fyne-io/terminal"
	gossh "golang.org/x/crypto/ssh"
)

// SessionTab manages a single SSH session and its terminal widget.
type SessionTab struct {
	Profile         config.Profile
	Terminal        *fyneterm.Terminal
	session         *internalssh.Session
	content         fyne.CanvasObject
	onClose         func()
	onContentChange func(fyne.CanvasObject) // called when content changes (e.g. error view)
	onZoom          func(delta float32)     // called when Ctrl+Scroll detected; set by App
	tabBtn          *widget.Button
	tabContainer    fyne.CanvasObject
	statusIndicator *canvas.Rectangle
	scrollWrapper   *ctrlScrollWrapper
	logger          *SessionLogger
	window          fyne.Window
	cfg             *config.Config
}

// NewSessionTab creates a new tab for the given profile.
func NewSessionTab(profile config.Profile, cfg *config.Config, window fyne.Window, onClose func()) *SessionTab {
	t := &SessionTab{
		Profile: profile,
		onClose: onClose,
		window:  window,
		cfg:     cfg,
	}

	term := fyneterm.New()
	t.Terminal = term
	t.setupTerminal(term)

	t.session = internalssh.NewSession(t.buildSessionConfig())

	t.scrollWrapper = newCtrlScrollWrapper(func(delta float32) {
		if t.onZoom != nil {
			t.onZoom(delta)
		}
	})
	scrollableTerm := container.NewVScroll(term)
	t.content = container.NewStack(scrollableTerm, t.scrollWrapper)
	indicator := canvas.NewRectangle(theme.WarningColor())
	indicator.CornerRadius = 5
	indicator.SetMinSize(fyne.NewSize(10, 10))
	// The button carries the session name as its label; placing it in an HBox
	// alongside the indicator avoids the "opaque button covers hidden label" problem
	// that occurs when stacking an empty button on top of a separate label widget.
	t.tabBtn = widget.NewButton(tabLabel(profile), nil)
	t.tabBtn.Importance = widget.LowImportance
	// The right-click overlay is appended by openSession once the detector is built.
	t.tabContainer = container.NewStack(
		container.NewHBox(indicator, t.tabBtn),
	)
	t.statusIndicator = indicator

	return t
}

// buildSessionConfig constructs an internalssh.SessionConfig for this tab's profile,
// including a TOFU host-key-changed dialog when host key verification is enabled.
func (st *SessionTab) buildSessionConfig() internalssh.SessionConfig {
	cfg := internalssh.SessionConfig{
		Host:                st.Profile.Host,
		Port:                st.Profile.Port,
		Username:            st.Profile.Username,
		Password:            st.Profile.Password,
		KeyPath:             st.Profile.KeyPath,
		HostKeyVerification: st.cfg.Settings.HostKeyVerification,
		KnownHostsPath:      st.cfg.KnownHostsPath,
	}
	if st.cfg.Settings.HostKeyVerification {
		win := st.window
		cfg.HostKeyChangedFn = func(host string, old, new gossh.PublicKey) bool {
			done := make(chan bool, 1)
			fyne.Do(func() {
				msg := fmt.Sprintf(
					"The host key for %s has changed!\n\nStored fingerprint:\n  %s\n\nPresented fingerprint:\n  %s\n\nThis may indicate a security issue (man-in-the-middle attack). Accept the new key?",
					host,
					gossh.FingerprintSHA256(old),
					gossh.FingerprintSHA256(new),
				)
				dialog.ShowConfirm("Host Key Changed", msg, func(ok bool) {
					done <- ok
				}, win)
			})
			return <-done
		}
	}
	return cfg
}

// setupTerminal registers the ReadWriter configurator on the terminal widget.
// Called at creation and on reconnect.
func (st *SessionTab) setupTerminal(term *fyneterm.Terminal) {
	term.SetReadWriter(fyneterm.ReadWriterConfiguratorFunc(func(r io.Reader, w io.WriteCloser) (io.Reader, io.WriteCloser) {
		return st.logger.WrapReader(r), NewPasteInterceptor(w, st.window, st.cfg.Settings.PasteDelayMs)
	}))
}

// Connect establishes the SSH connection and starts the terminal.
func (st *SessionTab) Connect() {
	go st.connectAsync()
}

func (st *SessionTab) connectAsync() {
	if err := st.session.Connect(); err != nil {
		st.showError(fmt.Sprintf("Connection failed:\n%s", err.Error()))
		return
	}

	pipes, err := st.session.StartPTY("xterm-256color", 80, 24)
	if err != nil {
		st.showError(fmt.Sprintf("PTY failed:\n%s", err.Error()))
		return
	}

	// Create logger just before RunWithConnection so the configurator can use it.
	logger, err := NewSessionLogger(
		st.cfg.Settings.LoggingEnabled,
		st.cfg.Settings.LogDir,
		st.cfg.Settings.LogFormat,
		st.Profile.Host,
	)
	if err != nil {
		log.Printf("session logging unavailable: %v", err)
		logger = &SessionLogger{enabled: false}
	}
	st.logger = logger

	// Poll the terminal widget's pixel size and forward PTY resize signals
	// whenever the dimensions change. This handles both the initial size (the
	// PTY was opened at 80×24) and live window resizing.
	stopResize := make(chan struct{})
	go func() {
		var lastCols, lastRows int
		cell := canvas.NewText("M", color.White)
		cell.TextStyle.Monospace = true

		ticker := time.NewTicker(50 * time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-ticker.C:
				size := st.Terminal.Size()
				cols, rows := termDimensions(size, cell)
				if cols > 0 && rows > 0 && (cols != lastCols || rows != lastRows) {
					lastCols, lastRows = cols, rows
					_ = st.session.ResizePTY(cols, rows)
				}
			case <-stopResize:
				return
			}
		}
	}()

	st.SetStatus(theme.SuccessColor())
	st.Terminal.RunWithConnection(pipes.Stdin, pipes.Stdout)
	close(stopResize)

	// Remote shell exited (e.g. user typed "exit") — close the tab.
	st.SetStatus(theme.ErrorColor())
	st.Close()
	if st.onClose != nil {
		st.onClose()
	}
}

func (st *SessionTab) showError(msg string) {
	st.SetStatus(theme.ErrorColor())
	errLabel := widget.NewLabel(msg)
	errLabel.Wrapping = fyne.TextWrapWord

	reconnectBtn := widget.NewButtonWithIcon("Reconnect", theme.ViewRefreshIcon(), func() {
		// Close old logger before reconnecting.
		st.logger.Close()
		st.logger = nil

		// Reset terminal.
		newTerm := fyneterm.New()
		st.Terminal = newTerm
		st.setupTerminal(newTerm)

		newSession := internalssh.NewSession(st.buildSessionConfig())
		st.session = newSession
		scrollableTerm := container.NewVScroll(newTerm)
		st.setContent(container.NewStack(scrollableTerm, st.scrollWrapper))
		go st.connectAsync()
	})

	closeBtn := widget.NewButtonWithIcon("Close Tab", theme.CancelIcon(), func() {
		st.Close()
		if st.onClose != nil {
			st.onClose()
		}
	})

	errContent := container.NewVBox(
		errLabel,
		container.NewHBox(reconnectBtn, closeBtn),
	)
	st.setContent(errContent)
}

// setContent updates the tab's displayed content and notifies the app if active.
func (st *SessionTab) setContent(c fyne.CanvasObject) {
	st.content = c
	if st.onContentChange != nil {
		st.onContentChange(c)
	}
}

// SetStatus updates the color of the tab's status indicator.
func (st *SessionTab) SetStatus(c color.Color) {
	if st.statusIndicator == nil {
		return
	}
	st.statusIndicator.FillColor = c
	st.statusIndicator.Refresh()
}

// Close terminates the SSH session and any active log file.
func (st *SessionTab) Close() {
	if st.session != nil {
		st.session.Close()
	}
	st.logger.Close()
}

func tabLabel(p config.Profile) string {
	if p.Name != "" {
		return p.Name
	}
	return fmt.Sprintf("%s@%s", p.Username, p.Host)
}

// ctrlScrollWrapper is a transparent, full-size overlay widget placed on top of the
// terminal. It intercepts scroll events: Ctrl+Scroll zooms the font; plain scroll
// passes through (the terminal widget handles its own scrollback separately).
type ctrlScrollWrapper struct {
	widget.BaseWidget
	onZoom func(delta float32)
}

func newCtrlScrollWrapper(onZoom func(delta float32)) *ctrlScrollWrapper {
	w := &ctrlScrollWrapper{onZoom: onZoom}
	w.ExtendBaseWidget(w)
	return w
}

func (w *ctrlScrollWrapper) Scrolled(ev *fyne.ScrollEvent) {
	if isCtrlDown() && w.onZoom != nil {
		delta := float32(1)
		if ev.Scrolled.DY < 0 {
			delta = -1
		}
		w.onZoom(delta)
	}
}

func (w *ctrlScrollWrapper) CreateRenderer() fyne.WidgetRenderer {
	// Transparent — renders nothing; exists only for scroll event interception.
	return widget.NewSimpleRenderer(container.NewWithoutLayout())
}

// termDimensions calculates the terminal grid size (cols, rows) from a pixel size,
// using the same formula as fyne-io/terminal's guessCellSize.
func termDimensions(size fyne.Size, cell *canvas.Text) (cols, rows int) {
	cs := cell.MinSize()
	if cs.Width <= 0 || cs.Height <= 0 {
		return 0, 0
	}
	cols = int(math.Floor(float64(size.Width) / float64(cs.Width)))
	rows = int(math.Floor(float64(size.Height) / float64(cs.Height)))
	return
}
