package main

import (
	"fmt"
	"io"
	"sort"
	"strconv"
	"strings"
)

var useColor = false

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

type serverInfo struct {
	ID                        string   `json:"id"`
	Name                      string   `json:"name"`
	Type                      string   `json:"type"`
	State                     string   `json:"state"`
	Port                      int      `json:"port"`
	PlayersOnline             int      `json:"playersOnline"`
	MinecraftVersion          string   `json:"minecraftVersion"`
	MinMemoryMB               float64  `json:"minMemoryMB"`
	MaxMemoryMB               float64  `json:"maxMemoryMB"`
	AutoRestart               bool     `json:"autoRestart"`
	AutoBackup                bool     `json:"autoBackup"`
	AutoStartOnAppLaunch      bool     `json:"autoStartOnAppLaunch"`
	AutoBackupIntervalMinutes int      `json:"autoBackupIntervalMinutes"`
	BackupRetention           int      `json:"backupRetention"`
	StopTimeoutSeconds        int      `json:"stopTimeoutSeconds"`
	Players                   []string `json:"players"`
}

func printServerTable(w io.Writer, servers []serverInfo) {
	if len(servers) == 0 {
		fmt.Fprintln(w, dim(T("Chưa có server nào trong app (hoặc tài khoản này không được phép thấy).")))
		return
	}
	rows := make([][]string, 0, len(servers))
	running, players := 0, 0
	for _, s := range servers {
		if s.State == "Running" {
			running++
		}
		players += s.PlayersOnline
		rows = append(rows, []string{bold(s.Name), s.Type, stateBadge(s.State), firstNonEmpty(s.MinecraftVersion, dim("-")), strconv.Itoa(s.Port), strconv.Itoa(s.PlayersOnline), dim(s.ID)})
	}
	printTable(w, []string{"NAME", "TYPE", "STATE", "MC", "PORT", "PLAYERS", "ID"}, rows)
	fmt.Fprintf(w, "\n  %s\n", dim(fmt.Sprintf(T("%d server · %d đang chạy · %d người chơi"), len(servers), running, players)))
}

func printStatus(w io.Writer, s serverInfo) {
	players := dim(T("(không ai)"))
	if len(s.Players) > 0 {
		players = strings.Join(s.Players, ", ")
	}
	mc := firstNonEmpty(s.MinecraftVersion, dim("?"))
	printBox(w, s.Name, []row{
		{T("Trạng thái"), stateBadge(s.State)},
		{T("Loại"), fmt.Sprintf("%s · Minecraft %s", s.Type, mc)},
		{"Port", strconv.Itoa(s.Port)},
		{T("RAM (Xms/Xmx)"), fmt.Sprintf("%.0f / %.0f MB", s.MinMemoryMB, s.MaxMemoryMB)},
		{fmt.Sprintf(T("Online (%d)"), s.PlayersOnline), players},
		{"ID", dim(s.ID)},
	})
	fmt.Fprintln(w)
	backup := yesNoColor(s.AutoBackup)
	if s.AutoBackup {
		backup += dim(fmt.Sprintf(T(" · mỗi %d phút · giữ %d bản"), s.AutoBackupIntervalMinutes, s.BackupRetention))
	}
	printBox(w, T("Tự động hoá"), []row{
		{T("Tự restart khi crash"), yesNoColor(s.AutoRestart)},
		{T("Backup tự động"), backup},
		{T("Tự Start cùng app"), yesNoColor(s.AutoStartOnAppLaunch)},
		{T("Chờ khi Stop"), fmt.Sprintf("%d s", s.StopTimeoutSeconds)},
	})
}

func sortedKeys(m map[string]string) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

func yesNo(b bool) string {
	if b {
		return T("bật")
	}
	return T("tắt")
}
