package ui

import (
	"fmt"
	"strconv"
	"time"

	"conga.ssh/internal/config"
	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/layout"
	"fyne.io/fyne/v2/widget"
)

// SessionManager is the two-column window for managing saved profiles and
// starting new SSH sessions.
type SessionManager struct {
	window    fyne.Window
	fyneApp   fyne.App
	cfg       *config.Config
	onConnect func(config.Profile)
}

// NewSessionManager creates a SessionManager.
func NewSessionManager(w fyne.Window, a fyne.App, cfg *config.Config, onConnect func(config.Profile)) *SessionManager {
	return &SessionManager{window: w, fyneApp: a, cfg: cfg, onConnect: onConnect}
}

// buildProfileFromEntries assembles a Profile from raw form strings.
// If portStr is not a valid port number it defaults to 22.
func buildProfileFromEntries(name, host, portStr, username, password, authType, keyPath string) config.Profile {
	port, err := strconv.Atoi(portStr)
	if err != nil || port < 1 || port > 65535 {
		port = 22
	}
	at := config.AuthPassword
	if authType == "SSH Key" {
		at = config.AuthKey
	}
	return config.Profile{
		Name:     name,
		Host:     host,
		Port:     port,
		Username: username,
		AuthType: at,
		Password: password,
		KeyPath:  keyPath,
	}
}

// Show opens the session manager window.
func (sm *SessionManager) Show() {
	mw := sm.fyneApp.NewWindow("Session Manager")
	mw.Resize(fyne.NewSize(720, 480))
	mw.CenterOnScreen()

	// ── Left column: new connection form ──────────────────────────────────────

	nameEntry := widget.NewEntry()
	nameEntry.SetPlaceHolder("Profile name")

	hostEntry := widget.NewEntry()
	hostEntry.SetPlaceHolder("hostname or IP")

	portEntry := widget.NewEntry()
	portEntry.SetText("22")

	userEntry := widget.NewEntry()
	userEntry.SetPlaceHolder("username")

	passEntry := widget.NewPasswordEntry()
	passEntry.SetPlaceHolder("password")

	authSelect := widget.NewSelect([]string{"Password", "SSH Key"}, nil)
	authSelect.SetSelected("Password")

	keyPathEntry := widget.NewEntry()
	keyPathEntry.SetPlaceHolder("path to private key file")
	keyPathEntry.Hide()

	authSelect.OnChanged = func(val string) {
		if val == "SSH Key" {
			keyPathEntry.Show()
			passEntry.SetPlaceHolder("Key passphrase (leave blank if none)")
		} else {
			keyPathEntry.Hide()
			passEntry.SetPlaceHolder("password")
		}
	}

	form := widget.NewForm(
		widget.NewFormItem("Name", nameEntry),
		widget.NewFormItem("Host", hostEntry),
		widget.NewFormItem("Port", portEntry),
		widget.NewFormItem("Username", userEntry),
		widget.NewFormItem("Auth", authSelect),
		widget.NewFormItem("Password", passEntry),
		widget.NewFormItem("Key Path", keyPathEntry),
	)

	// buildProfile reads the current form values into a Profile.
	buildProfile := func() config.Profile {
		return buildProfileFromEntries(
			nameEntry.Text, hostEntry.Text, portEntry.Text,
			userEntry.Text, passEntry.Text, authSelect.Selected, keyPathEntry.Text,
		)
	}

	// populateForm fills the left column form from a saved profile.
	populateForm := func(p config.Profile) {
		nameEntry.SetText(p.Name)
		hostEntry.SetText(p.Host)
		portEntry.SetText(strconv.Itoa(p.Port))
		userEntry.SetText(p.Username)
		if p.AuthType == config.AuthKey {
			authSelect.SetSelected("SSH Key")
			keyPathEntry.SetText(p.KeyPath)
			passEntry.SetText(p.Password)
		} else {
			authSelect.SetSelected("Password")
			passEntry.SetText(p.Password)
		}
	}

	// saveProfBtn is wired after refreshList is defined.
	saveProfBtn := widget.NewButton("Save Profile", nil)

	leftColContent := container.NewBorder(nil, saveProfBtn, nil, nil, container.NewScroll(form))
	leftCard := widget.NewCard("New Connection", "", leftColContent)

	// ── Right column: saved profiles list ─────────────────────────────────────

	var profiles []config.Profile
	selectedIdx := -1
	var profileBtns []*widget.Button
	profileVBox := container.NewVBox()

	// lastClick tracks timing for double-click detection across list rebuilds.
	var lastClickTime time.Time
	var lastClickIdx int = -1

	var refreshList func()

	refreshList = func() {
		profiles = sm.cfg.Profiles()
		selectedIdx = -1
		profileBtns = nil
		profileVBox.Objects = nil

		for i, p := range profiles {
			idx := i
			prof := p
			btn := widget.NewButton(prof.Name, nil)
			btn.Importance = widget.LowImportance
			btn.Alignment = widget.ButtonAlignLeading
			btn.OnTapped = func() {
				now := time.Now()
				if idx == lastClickIdx && now.Sub(lastClickTime) < 350*time.Millisecond {
					// Double-click: connect immediately and close.
					mw.Close()
					sm.onConnect(prof)
					return
				}
				lastClickTime = now
				lastClickIdx = idx

				// Single click: select and populate the connection form.
				selectedIdx = idx
				populateForm(prof)
				for j, b := range profileBtns {
					if j == idx {
						b.Importance = widget.HighImportance
					} else {
						b.Importance = widget.LowImportance
					}
					b.Refresh()
				}
			}
			profileBtns = append(profileBtns, btn)
			profileVBox.Objects = append(profileVBox.Objects, btn)
		}
		profileVBox.Refresh()
	}

	saveProfBtn.OnTapped = func() {
		p := buildProfile()
		if p.Name == "" {
			dialog.ShowInformation("Save Profile", "Please enter a profile name.", mw)
			return
		}
		sm.cfg.SaveProfile(p)
		_ = sm.cfg.Save()
		refreshList()
	}

	editBtn := widget.NewButton("Edit", func() {
		if selectedIdx >= 0 && selectedIdx < len(profiles) {
			populateForm(profiles[selectedIdx])
		}
	})

	deleteBtn := widget.NewButton("Delete", func() {
		if selectedIdx < 0 || selectedIdx >= len(profiles) {
			return
		}
		name := profiles[selectedIdx].Name
		dialog.ShowConfirm("Delete Profile",
			fmt.Sprintf("Delete saved profile %q?", name),
			func(ok bool) {
				if !ok {
					return
				}
				sm.cfg.DeleteProfile(name)
				_ = sm.cfg.Save()
				refreshList()
			}, mw)
	})
	deleteBtn.Importance = widget.DangerImportance

	refreshList()

	listActions := container.NewHBox(editBtn, deleteBtn)
	rightColContent := container.NewBorder(nil, listActions, nil, nil, container.NewVScroll(profileVBox))
	rightCard := widget.NewCard("Saved Sessions", "", rightColContent)

	// ── Body: left = new connection, right = saved sessions ───────────────────

	body := container.NewGridWithColumns(2, leftCard, rightCard)

	// ── Footer: Cancel + Connect ──────────────────────────────────────────────

	cancelBtn := widget.NewButton("Cancel", func() { mw.Close() })

	connectBtn := widget.NewButton("Connect", func() {
		p := buildProfile()
		if p.Host == "" {
			dialog.ShowInformation("Connect", "Please enter a hostname.", mw)
			return
		}
		mw.Close()
		sm.onConnect(p)
	})
	connectBtn.Importance = widget.HighImportance

	footer := container.NewHBox(layout.NewSpacer(), cancelBtn, connectBtn)

	mw.SetContent(container.NewBorder(nil, footer, nil, nil, body))
	mw.Show()
}
