package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// Profile stores how to reach one Redstone instance. The token is kept in the
// config file (mode 0600) - or point TokenFile at a file you manage yourself.
type Profile struct {
	Host      string `json:"host,omitempty"`
	Port      int    `json:"port,omitempty"`
	Token     string `json:"token,omitempty"`
	TokenFile string `json:"tokenFile,omitempty"`
	SSH       string `json:"ssh,omitempty"` // user@host - when set, connect through an SSH tunnel
	SSHPort   int    `json:"sshPort,omitempty"`
	Identity  string `json:"identity,omitempty"`
}

type ConfigFile struct {
	Default  string             `json:"default,omitempty"`
	Profiles map[string]Profile `json:"profiles"`
}

func configPath() (string, error) {
	if p := os.Getenv("REDSTONECTL_CONFIG"); p != "" {
		return p, nil
	}
	dir, err := os.UserConfigDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "redstonectl", "config.json"), nil
}

func LoadConfig() (*ConfigFile, error) {
	path, err := configPath()
	if err != nil {
		return nil, err
	}
	cfg := &ConfigFile{Profiles: map[string]Profile{}}
	data, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return cfg, nil
	}
	if err != nil {
		return nil, err
	}
	if err := json.Unmarshal(data, cfg); err != nil {
		return nil, fmt.Errorf("file cấu hình %s bị lỗi: %w", path, err)
	}
	if cfg.Profiles == nil {
		cfg.Profiles = map[string]Profile{}
	}
	return cfg, nil
}

func (c *ConfigFile) Save() error {
	path, err := configPath()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return err
	}
	data, err := json.MarshalIndent(c, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, data, 0o600)
}

func (c *ConfigFile) names() []string {
	names := make([]string, 0, len(c.Profiles))
	for n := range c.Profiles {
		names = append(names, n)
	}
	sort.Strings(names)
	return names
}

// Settings is the fully-resolved connection description.
type Settings struct {
	Host     string
	Port     int
	Token    string
	SSH      string
	SSHPort  int
	Identity string
}

func readTokenFile(path string) (string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(data)), nil
}
