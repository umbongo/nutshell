package ui

import (
	"fmt"
	"strconv"

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

	// ── Right column: connection form ────────────────────────────────────────

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

	// populateForm fills the right column from a saved profile.
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

	// ── Left column: saved profiles list ─────────────────────────────────────

	var profiles []config.Profile
	selectedIdx := -1

	list := widget.NewList(
		func() int { return len(profiles) },
		func() fyne.CanvasObject { return widget.NewLabel("") },
		func(id widget.ListItemID, obj fyne.CanvasObject) {
			obj.(*widget.Label).SetText(profiles[id].Name)
		},
	)

	refreshList := func() {
		profiles = sm.cfg.Profiles()
		selectedIdx = -1
		list.UnselectAll()
		list.Refresh()
	}

	list.OnSelected = func(id widget.ListItemID) {
		selectedIdx = int(id)
		populateForm(profiles[id])
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

	listTitle := widget.NewLabel("Saved Profiles")
	listTitle.TextStyle.Bold = true
	listActions := container.NewHBox(editBtn, deleteBtn)
	leftCol := container.NewBorder(listTitle, listActions, nil, nil, list)

	// ── Right column layout ───────────────────────────────────────────────────

	formTitle := widget.NewLabel("Connection Details")
	formTitle.TextStyle.Bold = true

	saveProfBtn := widget.NewButton("Save Profile", func() {
		p := buildProfile()
		if p.Name == "" {
			dialog.ShowInformation("Save Profile", "Please enter a profile name.", mw)
			return
		}
		sm.cfg.SaveProfile(p)
		_ = sm.cfg.Save()
		refreshList()
	})

	rightCol := container.NewBorder(formTitle, saveProfBtn, nil, nil, container.NewScroll(form))

	// ── Body: two equal columns ───────────────────────────────────────────────

	body := container.NewGridWithColumns(2, leftCol, rightCol)

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
