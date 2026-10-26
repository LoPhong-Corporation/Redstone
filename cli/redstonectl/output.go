package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"sort"
	"strings"
	"text/tabwriter"
)

var useColor = false

func initColor(noColorFlag bool) {
	if noColorFlag || os.Getenv("NO_COLOR") != "" {
		return
	}
	fi, err := os.Stdout.Stat()
	if err != nil || fi.Mode()&os.ModeCharDevice == 0 {
		return
	}
	useColor = enableVirtualTerminal()
}

func paint(code, s string) string {
	if !useColor {
		return s
	}
	return "\x1b[" + code + "m" + s + "\x1b[0m"
}

func colorState(state string) string {
	switch state {
	case "Running":
		return paint("32", state)
	case "Crashed":
		return paint("31", state)
	case "Stopped":
		return paint("90", state)
	default:
		return paint("33", state)
	}
}

// colorLogLine tints a console line the same way the app does.
func colorLogLine(line string) string {
	switch {
	case strings.Contains(line, "ERROR]") || strings.Contains(line, "FATAL]") || strings.Contains(line, "Exception"):
		return paint("31", line)
	case strings.Contains(line, "WARN]"):
		return paint("33", line)
	case strings.HasPrefix(line, "[manager]") || strings.HasPrefix(line, "[tunnel]") || strings.HasPrefix(line, "> "):
		return paint("36", line)
	case strings.Contains(line, "joined the game"):
		return paint("32", line)
	case strings.Contains(line, "left the game"):
		return paint("90", line)
	}
	return line
}

func printJSON(w io.Writer, raw json.RawMessage) {
	var v any
	if json.Unmarshal(raw, &v) != nil {
		fmt.Fprintln(w, string(raw))
		return
	}
	out, _ := json.MarshalIndent(v, "", "  ")
	fmt.Fprintln(w, string(out))
}

type serverInfo struct {
	ID               string   `json:"id"`
	Name             string   `json:"name"`
	Type             string   `json:"type"`
	State            string   `json:"state"`
	Port             int      `json:"port"`
	PlayersOnline    int      `json:"playersOnline"`
	MinecraftVersion string   `json:"minecraftVersion"`
	MinMemoryMB      float64  `json:"minMemoryMB"`
	MaxMemoryMB      float64  `json:"maxMemoryMB"`
	AutoRestart      bool     `json:"autoRestart"`
	AutoBackup       bool     `json:"autoBackup"`
	Players          []string `json:"players"`
}

func printServerTable(w io.Writer, servers []serverInfo) {
	if len(servers) == 0 {
		fmt.Fprintln(w, "Chưa có server nào trong app.")
		return
	}
	tw := tabwriter.NewWriter(w, 0, 0, 2, ' ', 0)
	fmt.Fprintln(tw, "NAME\tTYPE\tSTATE\tPORT\tPLAYERS\tID")
	for _, s := range servers {
		// tabwriter counts escape bytes, so pad the coloured cell manually.
		state := s.State
		pad := strings.Repeat(" ", max0(10-len(state)))
		fmt.Fprintf(tw, "%s\t%s\t%s%s\t%d\t%d\t%s\n", s.Name, s.Type, colorState(state), pad, s.Port, s.PlayersOnline, s.ID)
	}
	tw.Flush()
}

func max0(n int) int {
	if n < 0 {
		return 0
	}
	return n
}

func printStatus(w io.Writer, s serverInfo) {
	kv := func(k, v string) { fmt.Fprintf(w, "%-18s %s\n", k+":", v) }
	kv("Tên", s.Name)
	kv("ID", s.ID)
	kv("Trạng thái", colorState(s.State))
	kv("Loại", s.Type)
	if s.MinecraftVersion != "" {
		kv("Minecraft", s.MinecraftVersion)
	}
	kv("Port", fmt.Sprint(s.Port))
	kv("RAM (Xms/Xmx)", fmt.Sprintf("%.0f / %.0f MB", s.MinMemoryMB, s.MaxMemoryMB))
	kv("Tự restart khi crash", yesNo(s.AutoRestart))
	kv("Backup tự động", yesNo(s.AutoBackup))
	players := "(không ai)"
	if len(s.Players) > 0 {
		players = strings.Join(s.Players, ", ")
	}
	kv(fmt.Sprintf("Online (%d)", s.PlayersOnline), players)
}

func yesNo(b bool) string {
	if b {
		return "bật"
	}
	return "tắt"
}

func sortedKeys(m map[string]string) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}
