// redstonectl - remote control for the Redstone Minecraft Server Manager.
//
// Talks to the app's loopback-only control endpoint (JSON Lines over TCP,
// HMAC-authenticated). To reach another machine it opens an OpenSSH tunnel
// (ssh -L) with the system `ssh` client, so SSH supplies encryption, host-key
// checking and login. Only operations that exist in the app are available.
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

const version = "1.0.0"

type globalFlags struct {
	profile   string
	host      string
	port      int
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
}

func newGlobalFlagSet(g *globalFlags) *flag.FlagSet {
	fs := flag.NewFlagSet("redstonectl", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	fs.StringVar(&g.profile, "profile", "", "")
	fs.StringVar(&g.host, "host", "", "")
	fs.IntVar(&g.port, "port", 0, "")
	fs.StringVar(&g.token, "token", "", "")
	fs.StringVar(&g.tokenFile, "token-file", "", "")
	fs.StringVar(&g.ssh, "ssh", "", "")
	fs.IntVar(&g.sshPort, "ssh-port", 0, "")
	fs.StringVar(&g.identity, "ssh-identity", "", "")
	fs.BoolVar(&g.json, "json", false, "")
	fs.BoolVar(&g.noColor, "no-color", false, "")
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
	if err := fs.Parse(args); err != nil {
		fmt.Fprintln(os.Stderr, "Lỗi:", err)
		fmt.Fprintln(os.Stderr, "Dùng `redstonectl help` để xem hướng dẫn.")
		return 2
	}
	initColor(g.noColor)
	rest := fs.Args()

	if g.showVer {
		fmt.Println("redstonectl", version)
		return 0
	}
	if g.showHelp || len(rest) == 0 || rest[0] == "help" {
		printHelp(rest)
		return 0
	}
	switch rest[0] {
	case "version":
		fmt.Println("redstonectl", version)
		return 0
	case "profile":
		return exitCode(runProfile(rest[1:]))
	}

	settings, err := resolveSettings(&g)
	if err != nil {
		return exitCode(err)
	}

	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt)
	defer cancel()

	addr := net.JoinHostPort(settings.Host, strconv.Itoa(settings.Port))
	var tunnel *Tunnel
	if settings.SSH != "" {
		fmt.Fprintf(os.Stderr, "Đang mở đường hầm SSH tới %s...\n", settings.SSH)
		tunnel, err = StartTunnel(settings.SSH, settings.SSHPort, settings.Identity, settings.Port, 90*time.Second)
		if err != nil {
			return exitCode(err)
		}
		defer tunnel.Close()
		addr = tunnel.LocalAddr
	} else if ip := net.ParseIP(settings.Host); settings.Host != "localhost" && (ip == nil || !ip.IsLoopback()) {
		fmt.Fprintln(os.Stderr, "Cảnh báo: kết nối không phải loopback và không qua SSH - dữ liệu đi dưới dạng văn bản thường. Hãy dùng --ssh.")
	}

	client, err := Dial(addr, settings.Token, 10*time.Second)
	if err != nil {
		return exitCode(err)
	}
	defer client.Close()

	env := &Env{
		Client:        client,
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

func exitCode(err error) int {
	if err == nil {
		return 0
	}
	fmt.Fprintln(os.Stderr, "Lỗi:", strings.TrimPrefix(err.Error(), "usage: "))
	if errors.Is(err, errUsage) {
		return 2
	}
	return 1
}

func resolveSettings(g *globalFlags) (*Settings, error) {
	cfg, err := LoadConfig()
	if err != nil {
		return nil, err
	}
	name := firstNonEmpty(g.profile, os.Getenv("REDSTONE_PROFILE"), cfg.Default)
	var p Profile
	if name != "" {
		var ok bool
		p, ok = cfg.Profiles[name]
		if !ok {
			return nil, fmt.Errorf("không có profile %q (xem `redstonectl profile list`)", name)
		}
	}

	s := &Settings{
		Host:     firstNonEmpty(g.host, os.Getenv("REDSTONE_HOST"), p.Host, "127.0.0.1"),
		SSH:      firstNonEmpty(g.ssh, os.Getenv("REDSTONE_SSH"), p.SSH),
		Identity: firstNonEmpty(g.identity, os.Getenv("REDSTONE_SSH_IDENTITY"), p.Identity),
	}
	s.Port = firstPositive(g.port, envInt("REDSTONE_PORT"), p.Port, 25585)
	s.SSHPort = firstPositive(g.sshPort, envInt("REDSTONE_SSH_PORT"), p.SSHPort, 22)

	switch {
	case g.token != "":
		s.Token = g.token
	case g.tokenFile != "":
		if s.Token, err = readTokenFile(g.tokenFile); err != nil {
			return nil, err
		}
	case os.Getenv("REDSTONE_TOKEN") != "":
		s.Token = os.Getenv("REDSTONE_TOKEN")
	case os.Getenv("REDSTONE_TOKEN_FILE") != "":
		if s.Token, err = readTokenFile(os.Getenv("REDSTONE_TOKEN_FILE")); err != nil {
			return nil, err
		}
	case p.Token != "":
		s.Token = p.Token
	case p.TokenFile != "":
		if s.Token, err = readTokenFile(p.TokenFile); err != nil {
			return nil, err
		}
	}
	if s.Token == "" {
		return nil, errors.New("chưa có token. Dùng --token, --token-file, biến REDSTONE_TOKEN hoặc `redstonectl profile add` (token xem trong app: Cài đặt > Điều khiển từ xa)")
	}
	return s, nil
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
			fmt.Println("Chưa có profile nào. Thêm bằng: redstonectl profile add <tên> --ssh user@host --token ...")
			return nil
		}
		for _, n := range cfg.names() {
			p := cfg.Profiles[n]
			mark := " "
			if n == cfg.Default {
				mark = "*"
			}
			target := firstNonEmpty(p.SSH, firstNonEmpty(p.Host, "127.0.0.1"))
			via := "trực tiếp"
			if p.SSH != "" {
				via = "qua SSH"
			}
			token := "(chưa có)"
			if p.Token != "" {
				token = "đã lưu"
			} else if p.TokenFile != "" {
				token = "file " + p.TokenFile
			}
			fmt.Printf("%s %-14s %-28s %-10s port %d  token: %s\n", mark, n, target, via, firstPositive(p.Port, 25585), token)
		}
		return nil
	case "remove":
		if len(args) != 2 {
			return fmt.Errorf("%w: redstonectl profile remove <tên>", errUsage)
		}
		if _, ok := cfg.Profiles[args[1]]; !ok {
			return fmt.Errorf("không có profile %q", args[1])
		}
		delete(cfg.Profiles, args[1])
		if cfg.Default == args[1] {
			cfg.Default = ""
		}
		return cfg.Save()
	case "use":
		if len(args) != 2 {
			return fmt.Errorf("%w: redstonectl profile use <tên>", errUsage)
		}
		if _, ok := cfg.Profiles[args[1]]; !ok {
			return fmt.Errorf("không có profile %q", args[1])
		}
		cfg.Default = args[1]
		return cfg.Save()
	case "add":
		fs := newFlagSet("profile add")
		var p Profile
		makeDefault := fs.Bool("default", false, "")
		fs.StringVar(&p.Host, "host", "", "")
		fs.IntVar(&p.Port, "port", 0, "")
		fs.StringVar(&p.Token, "token", "", "")
		fs.StringVar(&p.TokenFile, "token-file", "", "")
		fs.StringVar(&p.SSH, "ssh", "", "")
		fs.IntVar(&p.SSHPort, "ssh-port", 0, "")
		fs.StringVar(&p.Identity, "ssh-identity", "", "")
		pos, err := parseInterspersed(fs, args[1:])
		if err != nil || len(pos) != 1 {
			return fmt.Errorf("%w: redstonectl profile add <tên> [--ssh user@host] [--token ..|--token-file ..] [--port N] [--default]", errUsage)
		}
		if p.Token == "" && p.TokenFile == "" {
			return fmt.Errorf("%w: cần --token hoặc --token-file", errUsage)
		}
		cfg.Profiles[pos[0]] = p
		if *makeDefault || cfg.Default == "" {
			cfg.Default = pos[0]
		}
		if err := cfg.Save(); err != nil {
			return err
		}
		path, _ := configPath()
		fmt.Printf("Đã lưu profile %q vào %s (quyền 0600).\n", pos[0], path)
		return nil
	}
	return fmt.Errorf("%w: lệnh profile không hợp lệ %q", errUsage, args[0])
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
		return nil, errors.New("thiếu dấu nháy đóng")
	}
	if inWord {
		words = append(words, cur.String())
	}
	return words, nil
}

func runShell(env *Env) error {
	fi, _ := os.Stdin.Stat()
	interactive := fi != nil && fi.Mode()&os.ModeCharDevice != 0
	if interactive {
		fmt.Println("redstonectl shell - gõ `help` để xem lệnh, `use <server>` để chọn server mặc định, `exit` để thoát.")
	}
	sc := bufio.NewScanner(os.Stdin)
	sc.Buffer(make([]byte, 64*1024), 1<<20)
	for {
		if interactive {
			prompt := "redstone> "
			if env.DefaultServer != "" {
				prompt = "redstone[" + env.DefaultServer + "]> "
			}
			fmt.Print(paint("36", prompt))
		}
		if !sc.Scan() {
			return sc.Err()
		}
		words, err := splitArgs(strings.TrimSpace(sc.Text()))
		if err != nil {
			fmt.Fprintln(os.Stderr, "Lỗi:", err)
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
			fmt.Fprintln(os.Stderr, "Lỗi:", strings.TrimPrefix(err.Error(), "usage: "))
			if env.Client.readErrSet() {
				return errors.New("mất kết nối tới app")
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

func printHelp(rest []string) {
	fmt.Print(`redstonectl ` + version + ` - điều khiển từ xa Redstone (Minecraft Server Manager)

CÁCH DÙNG
  redstonectl [tuỳ chọn chung] <lệnh> [tham số]

KẾT NỐI
  Cùng máy:    redstonectl --token <token> list
  Từ xa (SSH): redstonectl --ssh bạn@may-chu --token <token> list
  Lưu lại:     redstonectl profile add home --ssh bạn@may-chu --token <token> --default
               redstonectl list            (dùng profile mặc định)
  Token lấy trong app: Cài đặt > Điều khiển từ xa.

TUỲ CHỌN CHUNG
  --profile <tên>       dùng profile đã lưu           (env REDSTONE_PROFILE)
  --host <ip>           mặc định 127.0.0.1             (env REDSTONE_HOST)
  --port <n>            mặc định 25585                 (env REDSTONE_PORT)
  --token <t>           token truy cập                 (env REDSTONE_TOKEN)
  --token-file <file>   đọc token từ file              (env REDSTONE_TOKEN_FILE)
  --ssh <user@host>     đi qua đường hầm OpenSSH       (env REDSTONE_SSH)
  --ssh-port <n>        port SSH, mặc định 22
  --ssh-identity <key>  file khoá riêng SSH (-i)
  --json                in kết quả JSON thô
  --timeout <thời gian> chờ phản hồi mỗi lệnh, mặc định 30s
  --no-color            tắt màu
  REDSTONE_SERVER       server mặc định để bỏ qua tham số <server>

LỆNH
`)
	for _, c := range commands {
		fmt.Printf("  %-10s %-44s %s\n", c.Name, c.Args, c.Help)
	}
	fmt.Print(`  shell      [server]                                     Phiên tương tác (giữ một kết nối)
  profile    add|list|remove|use                          Quản lý profile kết nối
  help, version

<server> là tên (không phân biệt hoa/thường) hoặc id server trong app.
Chỉ những thao tác có trong app được hỗ trợ; không thể thêm/xoá server,
khôi phục backup hay đổi cài đặt app từ xa.
`)
}
