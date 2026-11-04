package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"os"
	"runtime"
	"strconv"
	"strings"
	"time"
)

func humanDuration(seconds int64) string {
	d := time.Duration(seconds) * time.Second
	switch {
	case d >= 24*time.Hour:
		return fmt.Sprintf("%dd %dh", int(d.Hours())/24, int(d.Hours())%24)
	case d >= time.Hour:
		return fmt.Sprintf("%dh %dm", int(d.Hours()), int(d.Minutes())%60)
	case d >= time.Minute:
		return fmt.Sprintf("%dm %ds", int(d.Minutes()), int(d.Seconds())%60)
	}
	return fmt.Sprintf("%ds", seconds)
}

func colourModeText() string {
	switch colourDepth() {
	case 24:
		return "truecolor"
	case 8:
		return "256 colours"
	}
	return T("tắt")
}

func targetText(s *Settings) string {
	if s.SSH != "" {
		return fmt.Sprintf("%s %s %s:%d", cyan(s.SSH), dim(T("qua SSH →")), "127.0.0.1", s.Port)
	}
	return fmt.Sprintf("%s:%d %s", s.Host, s.Port, dim(T("(trực tiếp)")))
}

func tokenSourceText(src string) string {
	switch src {
	case "":
		return red(T("(chưa có)"))
	case "flag":
		return T("tham số --token")
	case "env":
		return T("biến REDSTONE_TOKEN")
	case "env token-file":
		return T("biến REDSTONE_TOKEN_FILE")
	case "token-file":
		return T("tham số --token-file")
	case "profile":
		return T("profile")
	case "profile token-file":
		return T("file token của profile")
	}
	return src
}

// ------------------------------------------------------------------- info

type whoAmI struct {
	User       string `json:"user"`
	Role       string `json:"role"`
	AllServers bool   `json:"allServers"`
	Servers    []struct {
		ID   string `json:"id"`
		Name string `json:"name"`
	} `json:"servers"`
	Commands []string `json:"commands"`
	Protocol int      `json:"protocol"`
}

type appInfo struct {
	Name          string `json:"name"`
	Version       string `json:"version"`
	Protocol      int    `json:"protocol"`
	Language      string `json:"language"`
	UptimeSeconds int64  `json:"uptimeSeconds"`
	Servers       int    `json:"servers"`
	Running       int    `json:"serversRunning"`
	Players       int    `json:"playersOnline"`
	Sessions      int    `json:"sessions"`
}

func scopeText(w whoAmI) string {
	if w.AllServers {
		return green(T("mọi server"))
	}
	names := make([]string, 0, len(w.Servers))
	for _, s := range w.Servers {
		names = append(names, s.Name)
	}
	if len(names) == 0 {
		return yellow(T("không server nào"))
	}
	return yellow(strings.Join(names, ", "))
}

// runInfo prints a one-screen overview: CLI, OpenSSH, configuration, connection, the app and its servers.
// Everything that does not need the app is shown even when the connection fails.
func runInfo(g *globalFlags) error {
	settings, src, err := resolveSettingsDetailed(g)
	if err != nil {
		return err
	}
	ssh := sshInfo()
	cfgPath := src.ConfigPath
	if _, statErr := os.Stat(cfgPath); statErr != nil {
		cfgPath += " " + dim(T("(chưa có)"))
	}

	result := map[string]any{
		"cli":     map[string]any{"version": version, "go": runtime.Version(), "os": runtime.GOOS, "arch": runtime.GOARCH, "language": lang, "protocol": protocolVersion},
		"openssh": map[string]any{"found": ssh.Found, "path": ssh.Path, "version": ssh.Version},
		"connection": map[string]any{
			"host": settings.Host, "port": settings.Port, "ssh": settings.SSH, "user": settings.User, "profile": src.Profile, "tokenSource": src.TokenSource,
		},
	}

	var connErr error
	var who whoAmI
	var app appInfo
	var servers []serverInfo
	var latency time.Duration
	connected := false

	if settings.Token != "" {
		if c, err := openConnection(settings, g); err != nil {
			connErr = err
		} else {
			defer c.Close()
			connected = true
			start := time.Now()
			if _, err := c.Client.Call("ping", nil, g.timeout); err == nil {
				latency = time.Since(start)
			}
			if raw, err := c.Client.Call("whoami", nil, g.timeout); err == nil {
				_ = json.Unmarshal(raw, &who)
				result["account"] = json.RawMessage(raw)
			}
			if raw, err := c.Client.Call("app", nil, g.timeout); err == nil {
				_ = json.Unmarshal(raw, &app)
				result["app"] = json.RawMessage(raw)
			}
			if raw, err := c.Client.Call("list", nil, g.timeout); err == nil {
				var r struct {
					Servers []serverInfo `json:"servers"`
				}
				_ = json.Unmarshal(raw, &r)
				servers = r.Servers
				result["servers"] = r.Servers
			}
			result["connection"].(map[string]any)["latencyMs"] = latency.Milliseconds()
		}
	}

	if g.json {
		if connErr != nil {
			result["error"] = connErr.Error()
		}
		out, _ := json.Marshal(result)
		printJSON(os.Stdout, out)
		return connErr
	}

	if !g.noBanner && isTerminal(os.Stdout) {
		printBanner(os.Stdout)
	}
	o := os.Stdout
	printBox(o, "CLI", []row{
		{T("Phiên bản"), "redstonectl " + cyan(version)},
		{T("Bản dựng"), fmt.Sprintf("%s · %s/%s", runtime.Version(), runtime.GOOS, runtime.GOARCH)},
		{T("Giao thức"), fmt.Sprintf("v%d", protocolVersion)},
		{T("Ngôn ngữ"), lang},
		{T("Màu sắc"), colourModeText()},
	})
	fmt.Fprintln(o)

	sshRows := []row{}
	if ssh.Found {
		sshRows = append(sshRows, row{T("Trạng thái"), green("✓ ") + T("đã cài")}, row{T("Phiên bản"), ssh.Version}, row{T("Đường dẫn"), ssh.Path})
	} else {
		sshRows = append(sshRows, row{T("Trạng thái"), red("✗ ") + T("chưa cài")})
		if settings.SSH != "" {
			sshRows = append(sshRows, row{"", yellow(T("Cần để dùng --ssh - CLI sẽ hỏi và cài giúp khi bạn chạy lệnh."))})
		}
	}
	printBox(o, "OpenSSH Client", sshRows)
	fmt.Fprintln(o)

	profile := src.Profile
	if profile == "" {
		profile = dim(T("(không dùng)"))
	}
	connRows := []row{
		{T("Cấu hình"), cfgPath},
		{"Profile", profile},
		{T("Đích"), targetText(settings)},
		{T("Tài khoản"), cyan(settings.User)},
		{"Token", tokenSourceText(src.TokenSource)},
	}
	switch {
	case settings.Token == "":
		connRows = append(connRows, row{T("Kết nối"), red("✗ ") + T("chưa có token")})
	case connErr != nil:
		connRows = append(connRows, row{T("Kết nối"), red("✗ ") + firstLine(connErr.Error())})
	case connected:
		connRows = append(connRows, row{T("Kết nối"), green("✓ ") + fmt.Sprintf(T("đã xác thực · %d ms"), latency.Milliseconds())})
	}
	printBox(o, T("Kết nối"), connRows)
	fmt.Fprintln(o)

	if connected {
		printBox(o, T("Tài khoản & quyền"), []row{
			{T("Tài khoản"), cyan(who.User) + "  " + roleBadge(who.Role)},
			{T("Phạm vi"), scopeText(who)},
			{T("Số lệnh được phép"), fmt.Sprintf("%d  %s", len(who.Commands), dim("(redstonectl perms)"))},
		})
		fmt.Fprintln(o)
		printBox(o, "Redstone app", []row{
			{T("Phiên bản"), firstNonEmpty(app.Version, "?")},
			{T("Giao thức"), fmt.Sprintf("v%d", app.Protocol)},
			{T("Ngôn ngữ"), app.Language},
			{T("Thời gian chạy"), humanDuration(app.UptimeSeconds)},
			{T("Server"), fmt.Sprintf(T("%d (đang chạy: %d) · %d người chơi"), app.Servers, app.Running, app.Players)},
			{T("Phiên điều khiển"), fmt.Sprint(app.Sessions)},
		})
		fmt.Fprintln(o)
		printServerTable(o, servers)
	}
	return connErr
}

func firstLine(s string) string {
	if i := strings.IndexByte(s, '\n'); i >= 0 {
		return s[:i]
	}
	return s
}

// ----------------------------------------------------------------- doctor

type doctorStep struct {
	level  int // 1 = ok, 0 = failed, 2 = warning
	name   string
	detail string
	hint   string
}

func (s doctorStep) print() {
	mark := check(s.level == 1)
	if s.level == 2 {
		mark = warnMark()
	}
	fmt.Printf("  %s %s", mark, bold(s.name))
	if s.detail != "" {
		fmt.Printf("  %s", dim(s.detail))
	}
	fmt.Println()
	if s.hint != "" && s.level != 1 {
		fmt.Printf("      %s %s\n", yellow("→"), s.hint)
	}
}

// runDoctor checks every link of the chain in order and says what to fix.
func runDoctor(g *globalFlags) error {
	if !g.noBanner && isTerminal(os.Stdout) {
		printBanner(os.Stdout)
	}
	fmt.Println(heading(T("CHẨN ĐOÁN KẾT NỐI")))
	problems := 0
	report := func(s doctorStep) {
		s.print()
		if s.level == 0 {
			problems++
		}
	}

	report(doctorStep{level: 1, name: T("CLI"), detail: fmt.Sprintf("redstonectl %s · %s/%s · %s", version, runtime.GOOS, runtime.GOARCH, runtime.Version())})

	settings, src, err := resolveSettingsDetailed(g)
	if err != nil {
		report(doctorStep{level: 0, name: T("Cấu hình"), detail: err.Error(), hint: T("Sửa hoặc xoá file cấu hình, hoặc dùng --profile khác.")})
		return fmt.Errorf("%s", T("chẩn đoán dừng ở bước cấu hình"))
	}
	report(doctorStep{level: 1, name: T("Cấu hình"), detail: firstNonEmpty(src.ConfigPath) + profileNote(src.Profile)})

	if settings.Token == "" {
		report(doctorStep{level: 0, name: "Token", detail: T("chưa có"), hint: T("Dùng --token, --token-file, REDSTONE_TOKEN hoặc `redstonectl profile add`. Token xem trong app: Cài đặt > Điều khiển từ xa.")})
		return fmt.Errorf("%s", T("chẩn đoán dừng: thiếu token"))
	}
	report(doctorStep{level: 1, name: "Token", detail: tokenSourceText(src.TokenSource) + " · " + T("tài khoản ") + settings.User})

	addr := net.JoinHostPort(settings.Host, strconv.Itoa(settings.Port))
	var tunnel *Tunnel
	if settings.SSH != "" {
		ssh := sshInfo()
		if !ssh.Found {
			report(doctorStep{level: 0, name: "OpenSSH Client", detail: T("chưa cài"), hint: T("Chạy lại với --yes để CLI tự cài, hoặc cài thủ công.")})
			path, ierr := ensureSSH(g.yes)
			if ierr != nil {
				return fmt.Errorf("%s", T("chẩn đoán dừng: không có OpenSSH Client"))
			}
			_ = path
			ssh = SSHInfo{Found: true, Path: path, Version: "?"}
		} else {
			report(doctorStep{level: 1, name: "OpenSSH Client", detail: ssh.Version + " · " + ssh.Path})
		}
		sp := startSpinner(fmt.Sprintf(T("Đang mở đường hầm SSH tới %s..."), settings.SSH))
		tunnel, err = StartTunnel(ssh.Path, settings.SSH, settings.SSHPort, settings.Identity, settings.Port, 90*time.Second)
		sp.Stop()
		if err != nil {
			report(doctorStep{level: 0, name: T("Đường hầm SSH"), detail: firstLine(err.Error()), hint: T("Kiểm tra: SSH Server trên máy đích đang chạy, đúng user/port/khoá, và port điều khiển từ xa của app.")})
			return fmt.Errorf("%s", T("chẩn đoán dừng: không mở được đường hầm SSH"))
		}
		defer tunnel.Close()
		report(doctorStep{level: 1, name: T("Đường hầm SSH"), detail: settings.SSH + " → 127.0.0.1:" + strconv.Itoa(settings.Port)})
		addr = tunnel.LocalAddr
	} else {
		report(doctorStep{level: 2, name: T("Đường hầm SSH"), detail: T("không dùng (kết nối trực tiếp)"), hint: ""})
	}

	t0 := time.Now()
	nc, derr := net.DialTimeout("tcp", addr, 5*time.Second)
	if derr != nil {
		report(doctorStep{level: 0, name: T("Kết nối TCP"), detail: firstLine(derr.Error()), hint: T("App Redstone đã chạy và đã bật \"Điều khiển từ xa\" (Cài đặt > Điều khiển từ xa) với đúng port chưa?")})
		return fmt.Errorf("%s", T("chẩn đoán dừng: không kết nối được"))
	}
	nc.Close()
	report(doctorStep{level: 1, name: T("Kết nối TCP"), detail: fmt.Sprintf("%s · %d ms", addr, time.Since(t0).Milliseconds())})

	client, aerr := Dial(addr, settings.User, settings.Token, 10*time.Second)
	if aerr != nil {
		report(doctorStep{level: 0, name: T("Xác thực"), detail: firstLine(aerr.Error()), hint: T("Kiểm tra --user và --token khớp một tài khoản trong app. Sai 5 lần liên tiếp app sẽ khoá 30 giây.")})
		return fmt.Errorf("%s", T("chẩn đoán dừng: xác thực thất bại"))
	}
	defer client.Close()
	report(doctorStep{level: 1, name: T("Xác thực"), detail: client.User + " · " + client.Role + " · " + fmt.Sprintf("protocol v%d", protocolVersion)})

	if raw, cerr := client.Call("app", nil, g.timeout); cerr == nil {
		var app appInfo
		_ = json.Unmarshal(raw, &app)
		report(doctorStep{level: 1, name: "Redstone app", detail: fmt.Sprintf("%s · %s · %s", firstNonEmpty(app.Version, "?"), T("chạy được ")+humanDuration(app.UptimeSeconds), fmt.Sprintf(T("%d server"), app.Servers))})
	} else {
		report(doctorStep{level: 0, name: "Redstone app", detail: cerr.Error()})
	}

	if raw, cerr := client.Call("list", nil, g.timeout); cerr == nil {
		var r struct {
			Servers []serverInfo `json:"servers"`
		}
		_ = json.Unmarshal(raw, &r)
		level := 1
		hint := ""
		if len(r.Servers) == 0 {
			level, hint = 2, T("Tài khoản này không thấy server nào (app chưa có server, hoặc tài khoản bị giới hạn phạm vi).")
		}
		report(doctorStep{level: level, name: T("Server nhìn thấy"), detail: fmt.Sprint(len(r.Servers)), hint: hint})
	}

	fmt.Println()
	if problems > 0 {
		return fmt.Errorf(T("%d bước thất bại"), problems)
	}
	fmt.Println(green("✓ ") + bold(T("Mọi thứ hoạt động bình thường.")))
	return nil
}

func profileNote(profile string) string {
	if profile == "" {
		return ""
	}
	return " · profile " + profile
}

var _ = errors.New
