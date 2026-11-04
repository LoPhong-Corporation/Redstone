package main

import (
	"os/exec"
	"strings"
	"sync"
	"time"
)

// SSHInfo describes the OpenSSH client found on this machine.
type SSHInfo struct {
	Found   bool
	Path    string
	Version string // e.g. "OpenSSH_9.6p1"
	Full    string // full `ssh -V` line
}

var (
	sshInfoOnce  sync.Once
	sshInfoCache SSHInfo
)

// sshInfo locates ssh and asks it for its version (`ssh -V` prints to stderr). Cached.
func sshInfo() SSHInfo {
	sshInfoOnce.Do(func() {
		path := findSSH()
		if path == "" {
			return
		}
		sshInfoCache = SSHInfo{Found: true, Path: path, Version: "?"}
		cmd := exec.Command(path, "-V")
		done := make(chan []byte, 1)
		go func() {
			out, _ := cmd.CombinedOutput()
			done <- out
		}()
		select {
		case out := <-done:
			full := strings.TrimSpace(strings.Split(string(out), "\n")[0])
			sshInfoCache.Full = full
			if full != "" {
				sshInfoCache.Version = strings.TrimSpace(strings.Split(full, ",")[0])
			}
		case <-time.After(2 * time.Second):
			_ = cmd.Process.Kill()
		}
	})
	return sshInfoCache
}
