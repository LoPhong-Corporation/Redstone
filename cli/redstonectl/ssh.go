package main

import (
	"fmt"
	"net"
	"os"
	"os/exec"
	"strconv"
	"time"
)

// Tunnel is an OpenSSH port-forward: 127.0.0.1:<local> -> (ssh host) -> 127.0.0.1:<remote>.
// The app only listens on loopback, so SSH provides the encryption, the host-key
// check and the login - this CLI needs no crypto code of its own for remote use.
type Tunnel struct {
	cmd       *exec.Cmd
	LocalAddr string
}

func freePort() (int, error) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return 0, err
	}
	defer l.Close()
	return l.Addr().(*net.TCPAddr).Port, nil
}

// StartTunnel runs `ssh -N -L ...` using the system OpenSSH client and waits until the
// forwarded port accepts connections. ssh's stdin/stderr stay attached to the terminal
// so password / passphrase / host-key prompts work normally.
func StartTunnel(target string, sshPort int, identity string, remotePort int, wait time.Duration) (*Tunnel, error) {
	sshBin, err := exec.LookPath("ssh")
	if err != nil {
		return nil, fmt.Errorf("không tìm thấy lệnh \"ssh\" - hãy cài OpenSSH client (Windows 10/11: Settings > Optional features > OpenSSH Client)")
	}
	local, err := freePort()
	if err != nil {
		return nil, err
	}
	args := []string{
		"-N",
		"-L", fmt.Sprintf("127.0.0.1:%d:127.0.0.1:%d", local, remotePort),
		"-o", "ExitOnForwardFailure=yes",
		"-o", "ServerAliveInterval=15",
		"-o", "ServerAliveCountMax=3",
	}
	if sshPort != 0 && sshPort != 22 {
		args = append(args, "-p", strconv.Itoa(sshPort))
	}
	if identity != "" {
		args = append(args, "-i", identity)
	}
	args = append(args, target)

	cmd := exec.Command(sshBin, args...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stderr // keep our stdout clean for --json output
	cmd.Stderr = os.Stderr
	if err := cmd.Start(); err != nil {
		return nil, err
	}

	exited := make(chan error, 1)
	go func() { exited <- cmd.Wait() }()

	t := &Tunnel{cmd: cmd, LocalAddr: fmt.Sprintf("127.0.0.1:%d", local)}
	deadline := time.Now().Add(wait)
	for time.Now().Before(deadline) {
		select {
		case err := <-exited:
			return nil, fmt.Errorf("ssh đã thoát trước khi tạo được đường hầm: %v", err)
		default:
		}
		if conn, err := net.DialTimeout("tcp", t.LocalAddr, 300*time.Millisecond); err == nil {
			conn.Close()
			return t, nil
		}
		time.Sleep(200 * time.Millisecond)
	}
	t.Close()
	return nil, fmt.Errorf("hết %s mà đường hầm SSH vẫn chưa sẵn sàng", wait)
}

func (t *Tunnel) Close() {
	if t != nil && t.cmd != nil && t.cmd.Process != nil {
		_ = t.cmd.Process.Kill()
	}
}
