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
	tabBtn          *tabLabel
	tabContainer    fyne.CanvasObject
	tabHBox         *fyne.Container   // inner HBox: [tabBtn, loggingLabel, statusIndicator]
	tabBorder       *canvas.Rectangle // stroke-only border around the tab
	statusIndicator *canvas.Rectangle
	loggingLabel    *loggingBadge // tappable "L" badge — wired by App
	scrollWrapper   *ctrlScrollWrapper
	logger          *SessionLogger
	window          fyne.Window
	cfg             *config.Config
	connectedAt     time.Time
	connected       bool
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

	const indicatorSize = float32(16)

	// Connection status dot — coloured square on the right of the tab.
	indicator := canvas.NewRectangle(theme.WarningColor())
	indicator.CornerRadius = 3
	indicator.SetMinSize(fyne.NewSize(indicatorSize, indicatorSize))

	// Logging badge — tappable; "L" is always black so it reads on any background.
	lBadge := newLoggingBadge()

	t.tabBtn = newTabLabel(profileLabel(profile))

	// Inner HBox: [session name] [L badge] [connection dot].
	hbox := container.NewHBox(t.tabBtn, lBadge, indicator)

	// Stroke-only border rectangle; transparent fill so content shows through.
	tabBorder := canvas.NewRectangle(color.Transparent)
	tabBorder.StrokeColor = theme.DisabledColor()
	tabBorder.StrokeWidth = 1
	tabBorder.CornerRadius = 4

	// borderBox groups the visual layers; hover overlay is appended later by openSession.
	borderBox := container.NewStack(tabBorder, container.NewPadded(hbox))
	t.tabContainer = container.NewStack(borderBox)

	t.statusIndicator = indicator
	t.loggingLabel = lBadge
	t.tabBorder = tabBorder
	t.tabHBox = hbox

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
	st.connected = true
	st.connectedAt = time.Now()
	st.Terminal.RunWithConnection(pipes.Stdin, pipes.Stdout)
	close(stopResize)

	// Remote shell exited (e.g. user typed "exit") — close the tab.
	st.connected = false
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

// SetStatus updates the color of the tab's connection status dot.
func (st *SessionTab) SetStatus(c color.Color) {
	if st.statusIndicator == nil {
		return
	}
	st.statusIndicator.FillColor = c
	st.statusIndicator.Refresh()
}

// SetLoggingState updates the "L" badge to reflect the current logging state.
func (st *SessionTab) SetLoggingState(active bool) {
	if st.loggingLabel == nil {
		return
	}
	st.loggingLabel.SetActive(active)
}

// tooltipText returns the formatted tooltip string for the tab hover.
func (st *SessionTab) tooltipText() string {
	logPath := "Not logging"
	if p := st.logger.LogPath(); p != "" {
		logPath = p
	}
	duration := "Not connected"
	if st.connected && !st.connectedAt.IsZero() {
		duration = "Connected " + formatDuration(time.Since(st.connectedAt))
	}
	return fmt.Sprintf("User: %s\nHost: %s:%d\nLog: %s\n%s",
		st.Profile.Username, st.Profile.Host, st.Profile.Port, logPath, duration)
}

// Close terminates the SSH session and any active log file.
func (st *SessionTab) Close() {
	if st.session != nil {
		st.session.Close()
	}
	st.logger.Close()
}

func profileLabel(p config.Profile) string {
	if p.Name != "" {
		return p.Name
	}
	return fmt.Sprintf("%s@%s", p.Username, p.Host)
}

// formatDuration converts d to a human-readable string such as "4m 32s".
func formatDuration(d time.Duration) string {
	d = d.Round(time.Second)
	h := int(d.Hours())
	m := int(d.Minutes()) % 60
	s := int(d.Seconds()) % 60
	if h > 0 {
		return fmt.Sprintf("%dh %dm %ds", h, m, s)
	}
	if m > 0 {
		return fmt.Sprintf("%dm %ds", m, s)
	}
	return fmt.Sprintf("%ds", s)
}

// ── loggingBadge ─────────────────────────────────────────────────────────────
//
// loggingBadge is a tappable fixed-size square showing "L" in black on a
// coloured background: green when active, grey when inactive.
// OnTapped is wired by App.openSession to toggle the logger.

type loggingBadge struct {
	widget.BaseWidget
	OnTapped func()
	bg       *canvas.Rectangle
	lTxt     *canvas.Text
}

const logBadgeSize = float32(16)

func newLoggingBadge() *loggingBadge {
	b := &loggingBadge{
		bg:   canvas.NewRectangle(theme.DisabledColor()),
		lTxt: canvas.NewText("L", color.Black),
	}
	b.bg.CornerRadius = 3
	b.bg.SetMinSize(fyne.NewSize(logBadgeSize, logBadgeSize))
	b.lTxt.TextSize = 10
	b.lTxt.Alignment = fyne.TextAlignCenter
	b.ExtendBaseWidget(b)
	return b
}

func (b *loggingBadge) Tapped(*fyne.PointEvent) {
	if b.OnTapped != nil {
		b.OnTapped()
	}
}

// SetActive switches the background between success-green (active) and
// disabled-grey (inactive). The "L" text colour is always black.
func (b *loggingBadge) SetActive(active bool) {
	if active {
		b.bg.FillColor = theme.SuccessColor()
	} else {
		b.bg.FillColor = theme.DisabledColor()
	}
	b.bg.Refresh()
}

func (b *loggingBadge) CreateRenderer() fyne.WidgetRenderer {
	return widget.NewSimpleRenderer(
		container.NewStack(b.bg, container.NewCenter(b.lTxt)),
	)
}

// ── tabLabel ─────────────────────────────────────────────────────────────────
//
// tabLabel is a lightweight clickable label that renders at 80 % of the theme
// font size. It is used as the session-name button in the tab strip.

type tabLabel struct {
	widget.BaseWidget
	Importance widget.ButtonImportance
	OnTapped   func()
	text       string
}

func newTabLabel(text string) *tabLabel {
	l := &tabLabel{text: text, Importance: widget.LowImportance}
	l.ExtendBaseWidget(l)
	return l
}

// TextSize returns the label's font size: 80 % of the current theme text size.
func (l *tabLabel) TextSize() float32 {
	return theme.TextSize() * 0.8
}

func (l *tabLabel) Tapped(*fyne.PointEvent) {
	if l.OnTapped != nil {
		l.OnTapped()
	}
}

func (l *tabLabel) CreateRenderer() fyne.WidgetRenderer {
	txt := canvas.NewText(l.text, theme.ForegroundColor())
	txt.TextSize = l.TextSize()
	bg := canvas.NewRectangle(color.Transparent)
	return &tabLabelRenderer{lbl: l, txt: txt, bg: bg}
}

type tabLabelRenderer struct {
	lbl *tabLabel
	txt *canvas.Text
	bg  *canvas.Rectangle
}

func (r *tabLabelRenderer) Layout(size fyne.Size) {
	r.bg.Move(fyne.NewPos(0, 0))
	r.bg.Resize(size)
	pad := theme.InnerPadding()
	textH := r.txt.MinSize().Height
	r.txt.Move(fyne.NewPos(pad, (size.Height-textH)/2))
	r.txt.Resize(fyne.NewSize(size.Width-2*pad, textH))
}

func (r *tabLabelRenderer) MinSize() fyne.Size {
	r.txt.Text = r.lbl.text
	r.txt.TextSize = r.lbl.TextSize()
	inner := r.txt.MinSize()
	pad := theme.InnerPadding()
	return fyne.NewSize(inner.Width+2*pad, inner.Height+pad)
}

func (r *tabLabelRenderer) Refresh() {
	r.txt.Text = r.lbl.text
	r.txt.TextSize = r.lbl.TextSize()
	switch r.lbl.Importance {
	case widget.HighImportance:
		r.bg.FillColor = theme.PrimaryColor()
		r.txt.Color = theme.BackgroundColor()
	default:
		r.bg.FillColor = color.Transparent
		r.txt.Color = theme.ForegroundColor()
	}
	r.txt.Refresh()
	r.bg.Refresh()
}

func (r *tabLabelRenderer) Destroy() {}

func (r *tabLabelRenderer) Objects() []fyne.CanvasObject {
	return []fyne.CanvasObject{r.bg, r.txt}
}

// ── ctrlScrollWrapper ─────────────────────────────────────────────────────────

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
