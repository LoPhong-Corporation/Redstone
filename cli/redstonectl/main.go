// redstonectl - remote control for the Redstone Minecraft Server Manager.
//
// Talks to the app's loopback-only control endpoint (JSON Lines over TCP,
// HMAC-authenticated per account, role-based permissions). To reach another
// machine it opens an OpenSSH tunnel (ssh -L) with the system `ssh` client, so
// SSH supplies encryption, host-key checking and login. Only operations that
// exist in the app are available.
package main

import (
	"bufio"
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"time"
)

const version = "2.0.0"

type globalFlags struct {
	profile   string
	host      string
	port      int
	user      string
	token     string
	tokenFile string
	ssh       string
	sshPort   int
	identity  string
	json      bool
	noColor   bool
	timeout   time.Duration
	showHelp  bool
	showVer   bool
	yes       bool
	lang      string
	noBanner  bool
}

func newGlobalFlagSet(g *globalFlags) *flag.FlagSet {
	fs := flag.NewFlagSet("redstonectl", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	fs.StringVar(&g.profile, "profile", "", "")
	fs.StringVar(&g.host, "host", "", "")
	fs.IntVar(&g.port, "port", 0, "")
	fs.StringVar(&g.user, "user", "", "")
	fs.StringVar(&g.token, "token", "", "")
	fs.StringVar(&g.tokenFile, "token-file", "", "")
	fs.StringVar(&g.ssh, "ssh", "", "")
	fs.IntVar(&g.sshPort, "ssh-port", 0, "")
	fs.StringVar(&g.identity, "ssh-identity", "", "")
	fs.BoolVar(&g.json, "json", false, "")
	fs.BoolVar(&g.noColor, "no-color", false, "")
	fs.BoolVar(&g.yes, "yes", false, "")
	fs.BoolVar(&g.yes, "y", false, "")
	fs.StringVar(&g.lang, "lang", "", "")
	fs.BoolVar(&g.noBanner, "no-banner", false, "")
	fs.DurationVar(&g.timeout, "timeout", 30*time.Second, "")
	fs.BoolVar(&g.showHelp, "help", false, "")
	fs.BoolVar(&g.showHelp, "h", false, "")
	fs.BoolVar(&g.showVer, "version", false, "")
	return fs
}

func main() {
	os.Exit(run(os.Args[1:]))
}

func run(args []string) int {
	var g globalFlags
	fs := newGlobalFlagSet(&g)
	parseErr := fs.Parse(args)
	lang = detectLang(g.lang)
	initColor(g.noColor)
	buildCommands()
	if parseErr != nil {
		printError(parseErr.Error())
		printInfoErr(T("Dùng `redstonectl help` để xem hướng dẫn."))
		return 2
	}
	rest := fs.Args()

	if g.showVer {
		fmt.Println("redstonectl", version)
		return 0
	}
	if g.showHelp || len(rest) == 0 || rest[0] == "help" {
		if !g.noBanner && isTerminal(os.Stdout) {
			printBanner(os.Stdout)
		}
		printHelp(rest)
		return 0
	}
	switch rest[0] {
	case "version":
		fmt.Println("redstonectl", version)
		return 0
	case "banner":
		printBanner(os.Stdout)
		return 0
	case "profile":
		return exitCode(runProfile(rest[1:]))
	case "info":
		return exitCode(runInfo(&g))
	case "doctor":
		return exitCode(runDoctor(&g))
	}

	settings, _, err := resolveSettingsDetailed(&g)
	if err == nil && settings.Token == "" {
		err = errors.New(T("chưa có token. Dùng --token, --token-file, biến REDSTONE_TOKEN hoặc `redstonectl profile add` (token xem trong app: Cài đặt > Điều khiển từ xa)"))
	}
	if err != nil {
		return exitCode(err)
	}

	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt)
	defer cancel()

	c, err := openConnection(settings, &g)
	if err != nil {
		return exitCode(err)
	}
	defer c.Close()

	env := &Env{
		Client:        c.Client,
		Ctx:           ctx,
		Out:           os.Stdout,
		JSON:          g.json,
		Timeout:       g.timeout,
		DefaultServer: os.Getenv("REDSTONE_SERVER"),
	}

	if rest[0] == "shell" {
		if len(rest) > 1 {
			env.DefaultServer = rest[1]
		}
		return exitCode(runShell(env))
	}
	return exitCode(RunCommand(env, rest))
}

// conn bundles a live client with the SSH tunnel (if any) it runs through.
type conn struct {
	Client *Client
	Tunnel *Tunnel
	Addr   string
}

func (c *conn) Close() {
	if c.Client != nil {
		c.Client.Close()
	}
	if c.Tunnel != nil {
		c.Tunnel.Close()
	}
}

// openConnection opens the SSH tunnel when configured (offering to install OpenSSH if it is
// missing), then connects and authenticates.
func openConnection(settings *Settings, g *globalFlags) (*conn, error) {
	c := &conn{Addr: net.JoinHostPort(settings.Host, strconv.Itoa(settings.Port))}
	if settings.SSH != "" {
		sshBin, err := ensureSSH(g.yes)
		if err != nil {
			return nil, err
		}
		sp := startSpinner(fmt.Sprintf(T("Đang mở đường hầm SSH tới %s..."), settings.SSH))
		tunnel, err := StartTunnel(sshBin, settings.SSH, settings.SSHPort, settings.Identity, settings.Port, 90*time.Second)
		sp.Stop()
		if err != nil {
			return nil, err
		}
		c.Tunnel = tunnel
		c.Addr = tunnel.LocalAddr
	} else if ip := net.ParseIP(settings.Host); settings.Host != "localhost" && (ip == nil || !ip.IsLoopback()) {
		printWarning(T("kết nối không phải loopback và không qua SSH - dữ liệu đi dưới dạng văn bản thường. Hãy dùng --ssh."))
	}
	client, err := Dial(c.Addr, settings.User, settings.Token, 10*time.Second)
	if err != nil {
		c.Close()
		return nil, err
	}
	c.Client = client
	return c, nil
}

func exitCode(err error) int {
	if err == nil {
		return 0
	}
	printError(strings.TrimPrefix(err.Error(), "usage: "))
	var re *RemoteError
	if errors.As(err, &re) && re.Code == "forbidden" {
		if re.Required != "" {
			printInfoErr(fmt.Sprintf(T("Cần vai trò %s. Dùng --user với tài khoản có quyền đó (xem `redstonectl perms`)."), roleText(re.Required)))
		}
		return 3
	}
	if errors.Is(err, errUsage) {
		return 2
	}
	return 1
}

// ------------------------------------------------------------------ settings

// Sources records where each connection setting came from (never the secret itself).
type Sources struct {
	ConfigPath  string
	Profile     string
	TokenSource string // "flag", "env", "token-file", "profile", "profile token-file" or ""
}

func resolveSettings(g *globalFlags) (*Settings, error) {
	s, _, err := resolveSettingsDetailed(g)
	if err == nil && s.Token == "" {
		return nil, errors.New(T("chưa có token. Dùng --token, --token-file, biến REDSTONE_TOKEN hoặc `redstonectl profile add` (token xem trong app: Cài đặt > Điều khiển từ xa)"))
	}
	return s, err
}

func resolveSettingsDetailed(g *globalFlags) (*Settings, *Sources, error) {
	src := &Sources{}
	cfg, err := LoadConfig()
	if err != nil {
		return nil, src, err
	}
	src.ConfigPath, _ = configPath()
	name := firstNonEmpty(g.profile, os.Getenv("REDSTONE_PROFILE"), cfg.Default)
	var p Profile
	if name != "" {
		var ok bool
		p, ok = cfg.Profiles[name]
		if !ok {
			return nil, src, fmt.Errorf(T("không có profile %q (xem `redstonectl profile list`)"), name)
		}
		src.Profile = name
	}

	s := &Settings{
		Host:     firstNonEmpty(g.host, os.Getenv("REDSTONE_HOST"), p.Host, "127.0.0.1"),
		User:     firstNonEmpty(g.user, os.Getenv("REDSTONE_USER"), p.User, "admin"),
		SSH:      firstNonEmpty(g.ssh, os.Getenv("REDSTONE_SSH"), p.SSH),
		Identity: firstNonEmpty(g.identity, os.Getenv("REDSTONE_SSH_IDENTITY"), p.Identity),
	}
	s.Port = firstPositive(g.port, envInt("REDSTONE_PORT"), p.Port, 25585)
	s.SSHPort = firstPositive(g.sshPort, envInt("REDSTONE_SSH_PORT"), p.SSHPort, 22)

	switch {
	case g.token != "":
		s.Token, src.TokenSource = g.token, "flag"
	case g.tokenFile != "":
		if s.Token, err = readTokenFile(g.tokenFile); err != nil {
			return nil, src, err
		}
		src.TokenSource = "token-file"
	case os.Getenv("REDSTONE_TOKEN") != "":
		s.Token, src.TokenSource = os.Getenv("REDSTONE_TOKEN"), "env"
	case os.Getenv("REDSTONE_TOKEN_FILE") != "":
		if s.Token, err = readTokenFile(os.Getenv("REDSTONE_TOKEN_FILE")); err != nil {
			return nil, src, err
		}
		src.TokenSource = "env token-file"
	case p.Token != "":
		if s.Token, err = unprotectSecret(p.Token); err != nil {
			return nil, src, errors.New(T("không giải mã được token đã lưu trong profile (file cấu hình từ tài khoản Windows hoặc máy khác?) - hãy chạy lại `redstonectl profile add`"))
		}
		src.TokenSource = "profile"
	case p.TokenFile != "":
		if s.Token, err = readTokenFile(p.TokenFile); err != nil {
			return nil, src, err
		}
		src.TokenSource = "profile token-file"
	}
	return s, src, nil
}

func firstNonEmpty(values ...string) string {
	for _, v := range values {
		if v != "" {
			return v
		}
	}
	return ""
}

func firstPositive(values ...int) int {
	for _, v := range values {
		if v > 0 {
			return v
		}
	}
	return 0
}

func envInt(name string) int {
	n, _ := strconv.Atoi(os.Getenv(name))
	return n
}

// ------------------------------------------------------------------ profiles

func runProfile(args []string) error {
	if len(args) == 0 {
		return fmt.Errorf("%w: redstonectl profile add|list|remove|use ...", errUsage)
	}
	cfg, err := LoadConfig()
	if err != nil {
		return err
	}
	switch args[0] {
	case "list":
		if len(cfg.Profiles) == 0 {
			fmt.Println(T("Chưa có profile nào. Thêm bằng: redstonectl profile add <tên> --ssh user@host --token ..."))
			return nil
		}
		for _, n := range cfg.names() {
			p := cfg.Profiles[n]
			mark := " "
			if n == cfg.Default {
				mark = "*"
			}
			target := firstNonEmpty(p.SSH, firstNonEmpty(p.Host, "127.0.0.1"))
			via := T("trực tiếp")
			if p.SSH != "" {
				via = "qua SSH"
			}
			token := T("(chưa có)")
			if p.Token != "" {
				token = T("đã lưu")
				if isProtected(p.Token) {
					token = T("đã lưu (mã hoá)")
				}
			} else if p.TokenFile != "" {
				token = "file " + p.TokenFile
			}
			user := firstNonEmpty(p.User, "admin")
			fmt.Printf("%s %s %-28s %-10s port %d  %s  token: %s\n", mark, cyan(fmt.Sprintf("%-14s", n)), target, via, firstPositive(p.Port, 25585), dim("user: "+user), token)
		}
		return nil
	case "remove":
		if len(args) != 2 {
			return fmt.Errorf(T("%w: redstonectl profile remove <tên>"), errUsage)
		}
		if _, ok := cfg.Profiles[args[1]]; !ok {
			return fmt.Errorf(T("không có profile %q"), args[1])
		}
		delete(cfg.Profiles, args[1])
		if cfg.Default == args[1] {
			cfg.Default = ""
		}
		return cfg.Save()
	case "use":
		if len(args) != 2 {
			return fmt.Errorf(T("%w: redstonectl profile use <tên>"), errUsage)
		}
		if _, ok := cfg.Profiles[args[1]]; !ok {
			return fmt.Errorf(T("không có profile %q"), args[1])
		}
		cfg.Default = args[1]
		return cfg.Save()
	case "add":
		fs := newFlagSet("profile add")
		var p Profile
		makeDefault := fs.Bool("default", false, "")
		fs.StringVar(&p.Host, "host", "", "")
		fs.IntVar(&p.Port, "port", 0, "")
		fs.StringVar(&p.User, "user", "", "")
		fs.StringVar(&p.Token, "token", "", "")
		fs.StringVar(&p.TokenFile, "token-file", "", "")
		fs.StringVar(&p.SSH, "ssh", "", "")
		fs.IntVar(&p.SSHPort, "ssh-port", 0, "")
		fs.StringVar(&p.Identity, "ssh-identity", "", "")
		pos, err := parseInterspersed(fs, args[1:])
		if err != nil || len(pos) != 1 {
			return fmt.Errorf(T("%w: redstonectl profile add <tên> [--ssh user@host] [--token ..|--token-file ..] [--port N] [--default]"), errUsage)
		}
		if p.Token == "" && p.TokenFile == "" {
			return fmt.Errorf(T("%w: cần --token hoặc --token-file"), errUsage)
		}
		p.Token = protectSecret(p.Token) // DPAPI on Windows; plain (file is 0600) elsewhere
		cfg.Profiles[pos[0]] = p
		if *makeDefault || cfg.Default == "" {
			cfg.Default = pos[0]
		}
		if err := cfg.Save(); err != nil {
			return err
		}
		path, _ := configPath()
		fmt.Printf(T("Đã lưu profile %q vào %s (quyền 0600).\n"), pos[0], path)
		return nil
	}
	return fmt.Errorf(T("%w: lệnh profile không hợp lệ %q"), errUsage, args[0])
}

// --------------------------------------------------------------------- shell

func splitArgs(line string) ([]string, error) {
	var words []string
	var cur strings.Builder
	inWord := false
	var quote rune
	for _, r := range line {
		switch {
		case quote != 0:
			if r == quote {
				quote = 0
			} else {
				cur.WriteRune(r)
			}
		case r == '"' || r == '\'':
			quote = r
			inWord = true
		case r == ' ' || r == '\t':
			if inWord {
				words = append(words, cur.String())
				cur.Reset()
				inWord = false
			}
		default:
			cur.WriteRune(r)
			inWord = true
		}
	}
	if quote != 0 {
		return nil, errors.New(T("thiếu dấu nháy đóng"))
	}
	if inWord {
		words = append(words, cur.String())
	}
	return words, nil
}

func runShell(env *Env) error {
	interactive := isTerminal(os.Stdin)
	if interactive {
		if isTerminal(os.Stdout) {
			printBanner(os.Stdout)
		}
		fmt.Println(fmt.Sprintf(T("Đăng nhập: %s %s"), cyan(env.Client.User), roleBadge(env.Client.Role)))
		fmt.Println(T("redstonectl shell - gõ `help` để xem lệnh, `use <server>` để chọn server mặc định, `exit` để thoát."))
	}
	sc := bufio.NewScanner(os.Stdin)
	sc.Buffer(make([]byte, 64*1024), 1<<20)
	for {
		if interactive {
			prompt := env.Client.User + "@redstone"
			if env.DefaultServer != "" {
				prompt += "[" + env.DefaultServer + "]"
			}
			fmt.Print(paint("38;5;203", prompt) + dim("("+env.Client.Role+")") + paint("1;91", "> "))
		}
		if !sc.Scan() {
			return sc.Err()
		}
		words, err := splitArgs(strings.TrimSpace(sc.Text()))
		if err != nil {
			printError(err.Error())
			continue
		}
		if len(words) == 0 {
			continue
		}
		switch words[0] {
		case "exit", "quit":
			return nil
		case "help":
			printHelp(nil)
			continue
		case "use":
			if len(words) == 2 {
				env.DefaultServer = words[1]
			} else {
				env.DefaultServer = ""
			}
			continue
		}
		if err := RunCommand(env, words); err != nil {
			printError(strings.TrimPrefix(err.Error(), "usage: "))
			var re *RemoteError
			if errors.As(err, &re) && re.Code == "forbidden" && re.Required != "" {
				printInfoErr(fmt.Sprintf(T("Cần vai trò %s. Dùng --user với tài khoản có quyền đó (xem `redstonectl perms`)."), roleText(re.Required)))
			}
			if env.Client.readErrSet() {
				return errors.New(T("mất kết nối tới app"))
			}
		}
	}
}

func (c *Client) readErrSet() bool {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.closed
}

// ---------------------------------------------------------------------- help

func roleTag(role string) string {
	switch role {
	case "admin":
		return red("admin   ")
	case "operator":
		return yellow("operator")
	}
	return green("viewer  ")
}

func helpLine(name, args, role, help string) {
	fmt.Printf("  %s %s %s %s\n", cyan(fmt.Sprintf("%-10s", name)), dim(fmt.Sprintf("%-40s", args)), roleTag(role), T(help))
}

func flagLine(flagText, help string) {
	fmt.Printf("  %s %s\n", green(fmt.Sprintf("%-26s", flagText)), T(help))
}

func printHelp(_ []string) {
	fmt.Printf("%s %s - %s\n\n", bold(heading("redstonectl")), dim(version), T("điều khiển từ xa Redstone (Minecraft Server Manager)"))

	fmt.Println(heading(T("CÁCH DÙNG")))
	fmt.Printf("  redstonectl %s %s %s\n\n", dim("["+T("tuỳ chọn chung")+"]"), cyan("<"+T("lệnh")+">"), dim("["+T("tham số")+"]"))

	fmt.Println(heading(T("KẾT NỐI")))
	fmt.Printf("  %s  redstonectl --user admin --token <token> list\n", dim(fmt.Sprintf("%-12s", T("Cùng máy:"))))
	fmt.Printf("  %s  redstonectl --ssh bạn@may-chu --user admin --token <token> list\n", dim(fmt.Sprintf("%-12s", T("Từ xa (SSH):"))))
	fmt.Printf("  %s  redstonectl profile add home --ssh bạn@may-chu --user admin --token <token> --default\n", dim(fmt.Sprintf("%-12s", T("Lưu lại:"))))
	fmt.Printf("  %s\n\n", T("Token lấy trong app: Cài đặt > Điều khiển từ xa. Chưa có OpenSSH? CLI sẽ hỏi và cài giúp bạn."))

	fmt.Println(heading(T("TUỲ CHỌN CHUNG")))
	flagLine("--profile <tên>", "dùng profile đã lưu")
	flagLine("--user <tên>", "tên tài khoản trong app (mặc định admin)")
	flagLine("--token <t>", "token của tài khoản (env REDSTONE_TOKEN)")
	flagLine("--token-file <file>", "đọc token từ file")
	flagLine("--host <ip>", "mặc định 127.0.0.1")
	flagLine("--port <n>", "mặc định 25585")
	flagLine("--ssh <user@host>", "đi qua đường hầm OpenSSH (env REDSTONE_SSH)")
	flagLine("--ssh-port <n>", "port SSH, mặc định 22")
	flagLine("--ssh-identity <key>", "file khoá riêng SSH (-i)")
	flagLine("--yes, -y", "tự đồng ý khi hỏi (vd: cài OpenSSH)")
	flagLine("--lang vi|en", "ngôn ngữ (env REDSTONE_LANG)")
	flagLine("--json", "in kết quả JSON thô")
	flagLine("--timeout <thời gian>", "chờ phản hồi mỗi lệnh, mặc định 30s")
	flagLine("--no-color, --no-banner", "tắt màu / tắt logo")
	flagLine("REDSTONE_SERVER", "server mặc định để bỏ qua tham số <server>")
	fmt.Println()

	fmt.Println(heading(T("LỆNH")) + "  " + dim(T("(cột thứ ba: vai trò tối thiểu cần có)")))
	for _, c := range commands {
		helpLine(c.Name, c.Args, c.Role, c.Help)
	}
	helpLine("info", "", "viewer", "Chi tiết: CLI, OpenSSH, kết nối, app, server")
	helpLine("doctor", "", "viewer", "Chẩn đoán từng bước khi không kết nối được")
	helpLine("shell", "[server]", "viewer", "Phiên tương tác (giữ một kết nối)")
	helpLine("profile", "add|list|remove|use", "viewer", "Quản lý profile kết nối")
	helpLine("help, version", "", "viewer", "Trợ giúp / phiên bản")
	fmt.Println()
	fmt.Println(heading(T("PHÂN QUYỀN")))
	fmt.Printf("  %s %s\n", roleBadge("viewer"), T("chỉ xem: danh sách, trạng thái, console, người chơi, backup, server.properties"))
	fmt.Printf("  %s %s\n", roleBadge("operator"), T("+ Start/Stop/Restart, thao tác người chơi, tạo backup"))
	fmt.Printf("  %s %s\n", roleBadge("admin"), T("+ gửi lệnh console thô, sửa server.properties, xem nhật ký truy cập"))
	fmt.Println(dim("  " + T("App kiểm tra quyền ở mỗi lệnh và ghi nhật ký; tài khoản có thể bị giới hạn trong một số server.")))
	fmt.Println()
	fmt.Println(dim(T("<server> là tên (không phân biệt hoa/thường) hoặc id server trong app.")))
	fmt.Println(dim(T("Chỉ những thao tác có trong app được hỗ trợ; không thể thêm/xoá server, khôi phục backup hay đổi cài đặt app từ xa.")))
}
