package ui

import (
	"strconv"

	"conga.ssh/internal/config"
	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/widget"
)

// ConnectDialog presents a form for entering SSH connection details or picking a saved profile.
type ConnectDialog struct {
	window  fyne.Window
	cfg     *config.Config
	onConnect func(profile config.Profile)
}

func NewConnectDialog(w fyne.Window, cfg *config.Config, onConnect func(config.Profile)) *ConnectDialog {
	return &ConnectDialog{window: w, cfg: cfg, onConnect: onConnect}
}

// Show displays the connect dialog.
func (cd *ConnectDialog) Show() {
	nameEntry := widget.NewEntry()
	nameEntry.SetPlaceHolder("Profile name (optional)")

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

	// Saved profiles selector
	profiles := cd.cfg.Profiles()
	profileNames := make([]string, 0, len(profiles)+1)
	profileNames = append(profileNames, "-- New Connection --")
	for _, p := range profiles {
		profileNames = append(profileNames, p.Name)
	}

	profileSelect := widget.NewSelect(profileNames, func(selected string) {
		if selected == "-- New Connection --" {
			return
		}
		for _, p := range profiles {
			if p.Name == selected {
				nameEntry.SetText(p.Name)
				hostEntry.SetText(p.Host)
				portEntry.SetText(strconv.Itoa(p.Port))
				userEntry.SetText(p.Username)
				if p.AuthType == config.AuthKey {
					authSelect.SetSelected("SSH Key")
					keyPathEntry.SetText(p.KeyPath)
					passEntry.SetText(p.Password) // passphrase, may be empty
				} else {
					authSelect.SetSelected("Password")
					passEntry.SetText(p.Password)
				}
				break
			}
		}
	})
	profileSelect.SetSelected("-- New Connection --")

	saveProfile := widget.NewCheck("Save profile", nil)

	form := container.NewVBox(
		widget.NewLabel("Saved Profiles"),
		profileSelect,
		widget.NewSeparator(),
		widget.NewForm(
			widget.NewFormItem("Name", nameEntry),
			widget.NewFormItem("Host", hostEntry),
			widget.NewFormItem("Port", portEntry),
			widget.NewFormItem("Username", userEntry),
			widget.NewFormItem("Auth", authSelect),
			widget.NewFormItem("Password", passEntry),
			widget.NewFormItem("Key Path", keyPathEntry),
		),
		saveProfile,
	)

	d := dialog.NewCustomConfirm("Connect to SSH Server", "Connect", "Cancel", form, func(ok bool) {
		if !ok {
			return
		}
		port, err := strconv.Atoi(portEntry.Text)
		if err != nil || port < 1 || port > 65535 {
			port = 22
		}

		authType := config.AuthPassword
		if authSelect.Selected == "SSH Key" {
			authType = config.AuthKey
		}

		p := config.Profile{
			Name:     nameEntry.Text,
			Host:     hostEntry.Text,
			Port:     port,
			Username: userEntry.Text,
			AuthType: authType,
			Password: passEntry.Text,
			KeyPath:  keyPathEntry.Text,
		}

		if saveProfile.Checked && p.Name != "" {
			cd.cfg.SaveProfile(p)
			_ = cd.cfg.Save()
		}

		cd.onConnect(p)
	}, cd.window)

	d.Resize(fyne.NewSize(500, 420))
	d.Show()
}
