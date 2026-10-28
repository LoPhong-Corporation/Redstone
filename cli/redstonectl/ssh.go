package main

import (
	"bufio"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
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
func StartTunnel(sshBin, target string, sshPort int, identity string, remotePort int, wait time.Duration) (*Tunnel, error) {
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
			return nil, fmt.Errorf(T("ssh đã thoát trước khi tạo được đường hầm: %v"), err)
		default:
		}
		if conn, err := net.DialTimeout("tcp", t.LocalAddr, 300*time.Millisecond); err == nil {
			conn.Close()
			return t, nil
		}
		time.Sleep(200 * time.Millisecond)
	}
	t.Close()
	return nil, fmt.Errorf(T("hết %s mà đường hầm SSH vẫn chưa sẵn sàng"), wait)
}

func (t *Tunnel) Close() {
	if t != nil && t.cmd != nil && t.cmd.Process != nil {
		_ = t.cmd.Process.Kill()
	}
}

// ---------------------------------------------------------------- OpenSSH client

// findSSH locates the OpenSSH client: PATH first, then the places Windows puts it
// (a fresh install is not in this process's PATH yet).
func findSSH() string {
	if p, err := exec.LookPath("ssh"); err == nil {
		return p
	}
	if runtime.GOOS == "windows" {
		candidates := []string{
			filepath.Join(os.Getenv("SystemRoot"), "System32", "OpenSSH", "ssh.exe"),
			`C:\Program Files\OpenSSH\ssh.exe`,
		}
		for _, c := range candidates {
			if st, err := os.Stat(c); err == nil && !st.IsDir() {
				return c
			}
		}
	}
	return ""
}

type installPlan struct {
	display string   // what we show the user
	argv    []string // what we run
}

// planSSHInstall picks how to install the OpenSSH client on this system, or nil if we cannot.
func planSSHInstall() *installPlan {
	switch runtime.GOOS {
	case "windows":
		inner := "Add-WindowsCapability -Online -Name OpenSSH.Client~~~~0.0.1.0"
		return &installPlan{
			display: "powershell (Administrator): " + inner,
			argv: []string{"powershell.exe", "-NoProfile", "-Command",
				"Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile','-Command','" + inner + "'"},
		}
	case "linux":
		type pm struct {
			bin  string
			args []string
		}
		managers := []pm{
			{"apt-get", []string{"install", "-y", "openssh-client"}},
			{"dnf", []string{"install", "-y", "openssh-clients"}},
			{"yum", []string{"install", "-y", "openssh-clients"}},
			{"pacman", []string{"-S", "--noconfirm", "openssh"}},
			{"zypper", []string{"--non-interactive", "install", "openssh-clients"}},
			{"apk", []string{"add", "openssh-client"}},
		}
		for _, m := range managers {
			path, err := exec.LookPath(m.bin)
			if err != nil {
				continue
			}
			argv := append([]string{path}, m.args...)
			if os.Geteuid() != 0 {
				sudo, err := exec.LookPath("sudo")
				if err != nil {
					return nil
				}
				argv = append([]string{sudo}, argv...)
			}
			return &installPlan{display: strings.Join(append([]string{m.bin}, m.args...), " "), argv: argv}
		}
	}
	return nil
}

func askYesNo(question string, defaultYes bool) bool {
	hint := "[Y/n]"
	if !defaultYes {
		hint = "[y/N]"
	}
	fmt.Fprintf(os.Stderr, "%s %s ", bold(question), dim(hint))
	line, _ := bufio.NewReader(os.Stdin).ReadString('\n')
	switch strings.ToLower(strings.TrimSpace(line)) {
	case "":
		return defaultYes
	case "y", "yes", "c", "co", T("có"):
		return true
	}
	return false
}

// ensureSSH returns the path of the ssh client, offering to install it when it is missing.
func ensureSSH(assumeYes bool) (string, error) {
	if p := findSSH(); p != "" {
		return p, nil
	}
	printWarning(T("Chưa tìm thấy OpenSSH Client (lệnh `ssh`) - cần có để dùng --ssh."))

	plan := planSSHInstall()
	if plan == nil {
		if runtime.GOOS == "darwin" {
			return "", fmt.Errorf("%s", T("macOS thường có sẵn `ssh` - hãy kiểm tra biến PATH."))
		}
		return "", fmt.Errorf("%s", T("Không tự cài được trên hệ thống này. Hãy cài OpenSSH Client bằng trình quản lý gói của bạn (Windows: Settings > Optional features > OpenSSH Client)."))
	}
	printInfoErr(T("Lệnh cài đặt: ") + cyan(plan.display))

	if !assumeYes {
		if !isTerminal(os.Stdin) {
			return "", fmt.Errorf("%s", T("Không có terminal để hỏi. Chạy lại với --yes để tự cài OpenSSH Client, hoặc cài thủ công."))
		}
		if !askYesNo(T("Cài đặt OpenSSH Client ngay bây giờ?"), true) {
			return "", fmt.Errorf("%s", T("Đã huỷ - cần OpenSSH Client để dùng --ssh."))
		}
	}

	cmd := exec.Command(plan.argv[0], plan.argv[1:]...)
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stderr, os.Stderr
	if err := cmd.Run(); err != nil {
		return "", fmt.Errorf(T("Cài đặt thất bại: %v"), err)
	}
	if p := findSSH(); p != "" {
		printInfoErr(green("✓ ") + T("Đã cài OpenSSH Client."))
		return p, nil
	}
	return "", fmt.Errorf("%s", T("Đã chạy trình cài đặt nhưng vẫn chưa thấy `ssh` - hãy mở terminal mới (PATH chưa cập nhật) rồi chạy lại."))
}
