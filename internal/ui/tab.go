package ui

import (
	"fmt"
	"io"
	"log"
	"math"
	"time"

	"conga.ssh/internal/config"
	internalssh "conga.ssh/internal/ssh"
	fyneterm "github.com/fyne-io/terminal"
	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
	"image/color"
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

	sessionCfg := internalssh.SessionConfig{
		Host:                profile.Host,
		Port:                profile.Port,
		Username:            profile.Username,
		Password:            profile.Password,
		KeyPath:             profile.KeyPath,
		HostKeyVerification: cfg.Settings.HostKeyVerification,
	}
	t.session = internalssh.NewSession(sessionCfg)

	t.scrollWrapper = newCtrlScrollWrapper(func(delta float32) {
		if t.onZoom != nil {
			t.onZoom(delta)
		}
	})
	t.content = container.NewStack(term, t.scrollWrapper)
	t.tabBtn = widget.NewButton(tabLabel(profile), nil) // OnTapped set by App

	return t
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
		ticker := time.NewTicker(50 * time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-ticker.C:
				size := st.Terminal.Size()
				cols, rows := termDimensions(size)
				if cols > 0 && rows > 0 && (cols != lastCols || rows != lastRows) {
					lastCols, lastRows = cols, rows
					_ = st.session.ResizePTY(cols, rows)
				}
			case <-stopResize:
				return
			}
		}
	}()

	st.Terminal.RunWithConnection(pipes.Stdin, pipes.Stdout)
	close(stopResize)

	// Remote shell exited (e.g. user typed "exit") — close the tab.
	st.Close()
	if st.onClose != nil {
		st.onClose()
	}
}

func (st *SessionTab) showError(msg string) {
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

		newSession := internalssh.NewSession(internalssh.SessionConfig{
			Host:                st.Profile.Host,
			Port:                st.Profile.Port,
			Username:            st.Profile.Username,
			Password:            st.Profile.Password,
			KeyPath:             st.Profile.KeyPath,
			HostKeyVerification: st.cfg.Settings.HostKeyVerification,
		})
		st.session = newSession
		st.setContent(container.NewStack(newTerm, st.scrollWrapper))
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
func termDimensions(size fyne.Size) (cols, rows int) {
	cell := canvas.NewText("M", color.White)
	cell.TextStyle.Monospace = true
	cs := cell.MinSize()
	if cs.Width <= 0 || cs.Height <= 0 {
		return 0, 0
	}
	cols = int(math.Floor(float64(size.Width) / float64(cs.Width)))
	rows = int(math.Floor(float64(size.Height) / float64(cs.Height)))
	return
}
