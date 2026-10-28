package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"strings"
	"time"
)

// Env is everything a command needs to run.
type Env struct {
	Client        *Client
	Ctx           context.Context
	Out           io.Writer
	JSON          bool
	Timeout       time.Duration
	DefaultServer string // lets `shell` / REDSTONE_SERVER omit the <server> argument
}

type cmdSpec struct {
	Name        string
	Args        string
	Help        string
	NeedsServer bool
	Run         func(env *Env, server string, args []string) error
}

var errUsage = errors.New("usage")

// flags (without dashes) that consume the following word as their value
var valueFlags = map[string]bool{"n": true, "wait-timeout": true}

func usageErr(spec *cmdSpec) error {
	return fmt.Errorf("%w: redstonectl %s %s", errUsage, spec.Name, spec.Args)
}

func (e *Env) call(cmd string, params map[string]any) (json.RawMessage, error) {
	return e.Client.Call(cmd, params, e.Timeout)
}

// parseInterspersed lets flags appear before or after positional arguments.
func parseInterspersed(fs *flag.FlagSet, args []string) ([]string, error) {
	var positional []string
	for {
		if err := fs.Parse(args); err != nil {
			return nil, err
		}
		if fs.NArg() == 0 {
			return positional, nil
		}
		positional = append(positional, fs.Arg(0))
		args = fs.Args()[1:]
	}
}

func newFlagSet(name string) *flag.FlagSet {
	fs := flag.NewFlagSet(name, flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	return fs
}

func (e *Env) show(raw json.RawMessage, human func()) {
	if e.JSON {
		printJSON(e.Out, raw)
		return
	}
	human()
}

func (e *Env) sent(raw json.RawMessage) {
	e.show(raw, func() {
		var r struct {
			Sent string `json:"sent"`
		}
		_ = json.Unmarshal(raw, &r)
		fmt.Fprintln(e.Out, T("Đã gửi:"), r.Sent)
	})
}

// playerAction wraps the "player" command.
func playerAction(action string, needName bool, withReason bool) func(*Env, string, []string) error {
	return func(env *Env, server string, args []string) error {
		params := map[string]any{"server": server, "action": action}
		if needName {
			if len(args) < 1 {
				return fmt.Errorf(T("%w: thiếu tên người chơi"), errUsage)
			}
			params["name"] = args[0]
			if withReason && len(args) > 1 {
				params["reason"] = strings.Join(args[1:], " ")
			}
		}
		raw, err := env.call("player", params)
		if err != nil {
			return err
		}
		env.sent(raw)
		return nil
	}
}

func waitForState(env *Env, server string, timeout time.Duration, wantRestartCycle bool, ok func(string) bool) error {
	started := time.Now()
	deadline := started.Add(timeout)
	sawLeave := !wantRestartCycle
	var last string
	for time.Now().Before(deadline) {
		raw, err := env.call("status", map[string]any{"server": server})
		if err != nil {
			return err
		}
		var s serverInfo
		if err := json.Unmarshal(raw, &s); err != nil {
			return err
		}
		last = s.State
		if wantRestartCycle && s.State != "Running" {
			sawLeave = true
		}
		// A restart can finish between two polls; if we never saw the server leave
		// "Running" but it has been Running for a grace period, treat it as done.
		if wantRestartCycle && !sawLeave && time.Since(started) > 5*time.Second {
			sawLeave = true
		}
		if sawLeave && ok(s.State) {
			fmt.Fprintf(env.Out, T("Trạng thái: %s\n"), colorState(s.State))
			return nil
		}
		select {
		case <-env.Ctx.Done():
			return env.Ctx.Err()
		case <-time.After(time.Second):
		}
	}
	return fmt.Errorf(T("hết %s mà trạng thái vẫn là %q"), timeout, last)
}

func lifecycle(name string, target func(string) bool, cycle bool) func(*Env, string, []string) error {
	return func(env *Env, server string, args []string) error {
		fs := newFlagSet(name)
		wait := fs.Bool("wait", false, "")
		waitTimeout := fs.Duration("wait-timeout", 3*time.Minute, "")
		if _, err := parseInterspersed(fs, args); err != nil {
			return fmt.Errorf("%w: %v", errUsage, err)
		}
		raw, err := env.call(name, map[string]any{"server": server})
		if err != nil {
			return err
		}
		if env.JSON && !*wait {
			printJSON(env.Out, raw)
			return nil
		}
		if !*wait {
			fmt.Fprintf(env.Out, T("Đã yêu cầu %s. Dùng --wait để chờ xong, hoặc `status` để xem.\n"), name)
			return nil
		}
		return waitForState(env, server, *waitTimeout, cycle, target)
	}
}

var commands []*cmdSpec

func buildCommands() {
	commands = []*cmdSpec{
		{Name: "banner", Help: "In logo redstone ASCII", Run: func(env *Env, _ string, _ []string) error {
			printBanner(env.Out)
			return nil
		}},
		{Name: "ping", Help: T("Kiểm tra kết nối"), Run: func(env *Env, _ string, _ []string) error {
			start := time.Now()
			raw, err := env.call("ping", nil)
			if err != nil {
				return err
			}
			env.show(raw, func() { fmt.Fprintf(env.Out, "pong (%s)\n", time.Since(start).Round(time.Millisecond)) })
			return nil
		}},
		{Name: "list", Help: T("Liệt kê server và trạng thái"), Run: func(env *Env, _ string, _ []string) error {
			raw, err := env.call("list", nil)
			if err != nil {
				return err
			}
			env.show(raw, func() {
				var r struct {
					Servers []serverInfo `json:"servers"`
				}
				_ = json.Unmarshal(raw, &r)
				printServerTable(env.Out, r.Servers)
			})
			return nil
		}},
		{Name: "status", Args: "<server>", Help: T("Chi tiết một server"), NeedsServer: true, Run: func(env *Env, server string, _ []string) error {
			raw, err := env.call("status", map[string]any{"server": server})
			if err != nil {
				return err
			}
			env.show(raw, func() {
				var s serverInfo
				_ = json.Unmarshal(raw, &s)
				printStatus(env.Out, s)
			})
			return nil
		}},
		{Name: "start", Args: "<server> [--wait]", Help: T("Khởi động server"), NeedsServer: true,
			Run: lifecycle("start", func(s string) bool { return s == "Running" }, false)},
		{Name: "stop", Args: "<server> [--wait]", Help: T("Dừng server an toàn (lưu world)"), NeedsServer: true,
			Run: lifecycle("stop", func(s string) bool { return s == "Stopped" }, false)},
		{Name: "restart", Args: "<server> [--wait]", Help: T("Khởi động lại server"), NeedsServer: true,
			Run: lifecycle("restart", func(s string) bool { return s == "Running" }, true)},
		{Name: "console", Args: "<server> [-n 100] [-f]", Help: T("Xem log console (-f: theo dõi trực tiếp, Ctrl-C để thoát)"), NeedsServer: true, Run: runConsole},
		{Name: "send", Args: T("<server> <lệnh...>"), Help: T("Gửi một lệnh vào console của server"), NeedsServer: true, Run: func(env *Env, server string, args []string) error {
			if len(args) == 0 {
				return fmt.Errorf(T("%w: thiếu lệnh cần gửi"), errUsage)
			}
			raw, err := env.call("send", map[string]any{"server": server, "command": strings.Join(args, " ")})
			if err != nil {
				return err
			}
			env.sent(raw)
			return nil
		}},
		{Name: "players", Args: "<server>", Help: T("Người chơi online, whitelist, OP, danh sách cấm"), NeedsServer: true, Run: runPlayers},
		{Name: "op", Args: T("<server> <tên>"), Help: T("Cấp OP"), NeedsServer: true, Run: playerAction("op", true, false)},
		{Name: "deop", Args: T("<server> <tên>"), Help: T("Thu hồi OP"), NeedsServer: true, Run: playerAction("deop", true, false)},
		{Name: "kick", Args: T("<server> <tên> [lý do...]"), Help: T("Kick người chơi"), NeedsServer: true, Run: playerAction("kick", true, true)},
		{Name: "ban", Args: T("<server> <tên> [lý do...]"), Help: T("Ban người chơi"), NeedsServer: true, Run: playerAction("ban", true, true)},
		{Name: "pardon", Args: T("<server> <tên>"), Help: T("Gỡ ban người chơi"), NeedsServer: true, Run: playerAction("pardon", true, false)},
		{Name: "banip", Args: T("<server> <ip> [lý do...]"), Help: T("Ban địa chỉ IP"), NeedsServer: true, Run: playerAction("ban-ip", true, true)},
		{Name: "pardonip", Args: "<server> <ip>", Help: T("Gỡ ban địa chỉ IP"), NeedsServer: true, Run: playerAction("pardon-ip", true, false)},
		{Name: "whitelist", Args: T("<server> add|remove <tên> | on | off | reload"), Help: T("Quản lý whitelist"), NeedsServer: true, Run: runWhitelist},
		{Name: "gamemode", Args: T("<server> <survival|creative|adventure|spectator> <tên>"), Help: T("Đổi gamemode người chơi"), NeedsServer: true, Run: func(env *Env, server string, args []string) error {
			if len(args) != 2 {
				return fmt.Errorf(T("%w: redstonectl gamemode <server> <mode> <tên>"), errUsage)
			}
			raw, err := env.call("player", map[string]any{"server": server, "action": "gamemode", "mode": args[0], "name": args[1]})
			if err != nil {
				return err
			}
			env.sent(raw)
			return nil
		}},
		{Name: "msg", Args: T("<server> <tên> <nội dung...>"), Help: T("Nhắn riêng cho người chơi"), NeedsServer: true, Run: func(env *Env, server string, args []string) error {
			if len(args) < 2 {
				return fmt.Errorf(T("%w: redstonectl msg <server> <tên> <nội dung>"), errUsage)
			}
			raw, err := env.call("player", map[string]any{"server": server, "action": "msg", "name": args[0], "text": strings.Join(args[1:], " ")})
			if err != nil {
				return err
			}
			env.sent(raw)
			return nil
		}},
		{Name: "say", Args: T("<server> <nội dung...>"), Help: T("Thông báo tới toàn server"), NeedsServer: true, Run: func(env *Env, server string, args []string) error {
			if len(args) == 0 {
				return fmt.Errorf(T("%w: thiếu nội dung"), errUsage)
			}
			raw, err := env.call("player", map[string]any{"server": server, "action": "say", "text": strings.Join(args, " ")})
			if err != nil {
				return err
			}
			env.sent(raw)
			return nil
		}},
		{Name: "backup", Args: "<server>", Help: T("Tạo backup ngay (an toàn khi server đang chạy)"), NeedsServer: true, Run: func(env *Env, server string, _ []string) error {
			fmt.Fprintln(os.Stderr, T("Đang tạo backup..."))
			old := env.Timeout
			env.Timeout = 30 * time.Minute // compressing a big world can take a while
			defer func() { env.Timeout = old }()
			raw, err := env.call("backup", map[string]any{"server": server})
			if err != nil {
				return err
			}
			env.show(raw, func() {
				var r struct {
					Name string `json:"name"`
				}
				_ = json.Unmarshal(raw, &r)
				fmt.Fprintln(env.Out, T("Đã tạo:"), r.Name)
			})
			return nil
		}},
		{Name: "backups", Args: "<server>", Help: T("Danh sách backup"), NeedsServer: true, Run: func(env *Env, server string, _ []string) error {
			raw, err := env.call("backups", map[string]any{"server": server})
			if err != nil {
				return err
			}
			env.show(raw, func() {
				var r struct {
					Backups []struct {
						Name      string  `json:"name"`
						SizeBytes float64 `json:"sizeBytes"`
					} `json:"backups"`
				}
				_ = json.Unmarshal(raw, &r)
				if len(r.Backups) == 0 {
					fmt.Fprintln(env.Out, T("Chưa có backup nào."))
				}
				for _, b := range r.Backups {
					fmt.Fprintf(env.Out, "%-40s %8.1f MB\n", b.Name, b.SizeBytes/1048576)
				}
			})
			return nil
		}},
		{Name: "props", Args: T("<server> [khoá]"), Help: T("Xem server.properties (hoặc một khoá)"), NeedsServer: true, Run: func(env *Env, server string, args []string) error {
			raw, err := env.call("props", map[string]any{"server": server})
			if err != nil {
				return err
			}
			var r struct {
				Properties map[string]string `json:"properties"`
			}
			_ = json.Unmarshal(raw, &r)
			if len(args) == 1 {
				v, ok := r.Properties[args[0]]
				if !ok {
					return fmt.Errorf(T("không có khoá %q trong server.properties"), args[0])
				}
				if env.JSON {
					out, _ := json.Marshal(map[string]string{args[0]: v})
					printJSON(env.Out, out)
				} else {
					fmt.Fprintln(env.Out, v)
				}
				return nil
			}
			env.show(raw, func() {
				for _, k := range sortedKeys(r.Properties) {
					fmt.Fprintf(env.Out, "%s=%s\n", k, r.Properties[k])
				}
			})
			return nil
		}},
		{Name: "prop-set", Args: T("<server> <khoá> <giá trị...>"), Help: T("Đặt một khoá server.properties (có hiệu lực sau khi restart)"), NeedsServer: true, Run: func(env *Env, server string, args []string) error {
			if len(args) < 2 {
				return fmt.Errorf(T("%w: redstonectl prop-set <server> <khoá> <giá trị>"), errUsage)
			}
			raw, err := env.call("prop-set", map[string]any{"server": server, "key": args[0], "value": strings.Join(args[1:], " ")})
			if err != nil {
				return err
			}
			env.show(raw, func() { fmt.Fprintf(env.Out, T("Đã đặt %s. Restart server để áp dụng.\n"), args[0]) })
			return nil
		}},
	}
}

func findCommand(name string) *cmdSpec {
	for _, c := range commands {
		if c.Name == name {
			return c
		}
	}
	return nil
}

// RunCommand executes one command line (already split into words).
func RunCommand(env *Env, words []string) error {
	if len(words) == 0 {
		return nil
	}
	spec := findCommand(words[0])
	if spec == nil {
		return fmt.Errorf(T("%w: lệnh không tồn tại %q (xem `redstonectl help`)"), errUsage, words[0])
	}
	args := words[1:]
	server := ""
	if spec.NeedsServer {
		if env.DefaultServer != "" {
			server = env.DefaultServer
		} else {
			// first non-flag word is the server
			idx := -1
			for i := 0; i < len(args); i++ {
				a := args[i]
				if strings.HasPrefix(a, "-") {
					if valueFlags[strings.TrimLeft(a, "-")] && !strings.Contains(a, "=") {
						i++ // skip this flag's value so it is not mistaken for the server
					}
					continue
				}
				idx = i
				break
			}
			if idx < 0 {
				return usageErr(spec)
			}
			server = args[idx]
			args = append(append([]string{}, args[:idx]...), args[idx+1:]...)
		}
	}
	return spec.Run(env, server, args)
}

func runWhitelist(env *Env, server string, args []string) error {
	bad := fmt.Errorf(T("%w: redstonectl whitelist <server> add|remove <tên> | on | off | reload"), errUsage)
	if len(args) == 0 {
		return bad
	}
	action := ""
	name := ""
	switch args[0] {
	case "add", "remove":
		if len(args) != 2 {
			return bad
		}
		action, name = "whitelist-"+args[0], args[1]
	case "on", "off", "reload":
		action = "whitelist-" + args[0]
	default:
		return bad
	}
	params := map[string]any{"server": server, "action": action}
	if name != "" {
		params["name"] = name
	}
	raw, err := env.call("player", params)
	if err != nil {
		return err
	}
	env.sent(raw)
	return nil
}

func runPlayers(env *Env, server string, _ []string) error {
	raw, err := env.call("players", map[string]any{"server": server})
	if err != nil {
		return err
	}
	env.show(raw, func() {
		var r struct {
			Online    []string                `json:"online"`
			Whitelist []struct{ Name string } `json:"whitelist"`
			Ops       []struct {
				Name  string
				Level int
			} `json:"ops"`
			Banned []struct {
				Name   string
				Reason string
			} `json:"banned"`
			BannedIps []struct {
				Name   string
				Reason string
			} `json:"bannedIps"`
		}
		_ = json.Unmarshal(raw, &r)
		join := func(items []string) string {
			if len(items) == 0 {
				return T("(trống)")
			}
			return strings.Join(items, ", ")
		}
		var wl, ops, banned, ips []string
		for _, w := range r.Whitelist {
			wl = append(wl, w.Name)
		}
		for _, o := range r.Ops {
			ops = append(ops, fmt.Sprintf("%s(%d)", o.Name, o.Level))
		}
		for _, b := range r.Banned {
			banned = append(banned, b.Name)
		}
		for _, b := range r.BannedIps {
			ips = append(ips, b.Name)
		}
		fmt.Fprintf(env.Out, "Online (%d): %s\n", len(r.Online), join(r.Online))
		fmt.Fprintf(env.Out, "Whitelist:    %s\n", join(wl))
		fmt.Fprintf(env.Out, "Operators:    %s\n", join(ops))
		fmt.Fprintf(env.Out, T("Bị cấm:       %s\n"), join(banned))
		fmt.Fprintf(env.Out, T("IP bị cấm:    %s\n"), join(ips))
	})
	return nil
}

func runConsole(env *Env, server string, args []string) error {
	fs := newFlagSet("console")
	lines := fs.Int("n", 100, "")
	follow := fs.Bool("f", false, "")
	if _, err := parseInterspersed(fs, args); err != nil {
		return fmt.Errorf("%w: %v", errUsage, err)
	}

	// Subscribe BEFORE fetching the tail so no line is lost in between.
	if *follow {
		if _, err := env.call("subscribe", map[string]any{"server": server}); err != nil {
			return err
		}
		defer env.call("unsubscribe", nil)
	}
	raw, err := env.call("console", map[string]any{"server": server, "lines": *lines})
	if err != nil {
		return err
	}
	var r struct {
		Lines []string `json:"lines"`
	}
	_ = json.Unmarshal(raw, &r)
	if env.JSON && !*follow {
		printJSON(env.Out, raw)
		return nil
	}
	for _, l := range r.Lines {
		fmt.Fprintln(env.Out, colorLogLine(l))
	}
	if !*follow {
		return nil
	}

	ctx, stop := signal.NotifyContext(env.Ctx, os.Interrupt)
	defer stop()
	for {
		select {
		case <-ctx.Done():
			fmt.Fprintln(env.Out)
			return nil
		case ev, ok := <-env.Client.Events:
			if !ok {
				return errors.New(T("mất kết nối tới app"))
			}
			switch ev.Event {
			case "console":
				fmt.Fprintln(env.Out, colorLogLine(ev.Line))
			case "state":
				fmt.Fprintln(env.Out, paint("35", T("[trạng thái] "))+colorState(ev.State))
			}
		}
	}
}
