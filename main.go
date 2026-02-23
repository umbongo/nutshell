package main

import (
	"log"
	"os"
	"path/filepath"

	"conga.ssh/internal/config"
	"conga.ssh/internal/crypto"
	"conga.ssh/internal/ui"
)

func main() {
	// Resolve settings.ini path relative to the executable location.
	exePath, err := os.Executable()
	if err != nil {
		log.Fatalf("cannot determine executable path: %v", err)
	}
	settingsPath := filepath.Join(filepath.Dir(exePath), "settings.ini")

	key := crypto.MachineKey()

	cfg, err := config.Load(settingsPath, key)
	if err != nil {
		log.Fatalf("failed to load settings: %v", err)
	}

	application := ui.NewApp(cfg)
	application.Run()
}
