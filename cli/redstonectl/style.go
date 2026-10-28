package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"regexp"
	"strings"
)

var useColorErr = false

func initColor(noColorFlag bool) {
	if noColorFlag || os.Getenv("NO_COLOR") != "" {
		return
	}
	if isTerminal(os.Stdout) {
		useColor = enableVirtualTerminal()
	}
	if isTerminal(os.Stderr) {
		useColorErr = enableVirtualTerminal()
	}
}

func isTerminal(f *os.File) bool {
	fi, err := f.Stat()
	return err == nil && fi.Mode()&os.ModeCharDevice != 0
}

func wrapColor(enabled bool, code, s string) string {
	if !enabled || s == "" {
		return s
	}
	return "\x1b[" + code + "m" + s + "\x1b[0m"
}

// stdout styles
func bold(s string) string    { return paint("1", s) }
func dim(s string) string     { return paint("90", s) }
func red(s string) string     { return paint("31", s) }
func green(s string) string   { return paint("32", s) }
func yellow(s string) string  { return paint("33", s) }
func blue(s string) string    { return paint("34", s) }
func magenta(s string) string { return paint("35", s) }
func cyan(s string) string    { return paint("36", s) }

func okMark() string   { return green("✓") }
func warnMark() string { return yellow("!") }

// heading: bold red like the app icon, used for section titles.
func heading(s string) string { return paint("1;91", s) }

// stderr styles
func printError(msg string) {
	fmt.Fprintln(os.Stderr, wrapColor(useColorErr, "1;31", "✗ "+T("Lỗi:")), msg)
}

func printWarning(msg string) {
	fmt.Fprintln(os.Stderr, wrapColor(useColorErr, "1;33", "! "+T("Cảnh báo:")), msg)
}

func printInfoErr(msg string) {
	fmt.Fprintln(os.Stderr, wrapColor(useColorErr, "36", "› ")+msg)
}

var (
	jsonKeyRe = regexp.MustCompile(`^(\s*)("(?:[^"\\]|\\.)*")(:\s*)(.*)$`)
	jsonNumRe = regexp.MustCompile(`^-?\d+(\.\d+)?([eE][+-]?\d+)?,?$`)
)

func colorJSONValue(v string) string {
	trim := strings.TrimSuffix(v, ",")
	comma := strings.TrimPrefix(v, trim)
	switch {
	case strings.HasPrefix(trim, `"`):
		return green(trim) + comma
	case trim == "true" || trim == "false" || trim == "null":
		return magenta(trim) + comma
	case jsonNumRe.MatchString(v):
		return yellow(trim) + comma
	}
	return v
}

// printJSON pretty-prints raw JSON; keys cyan, strings green, numbers yellow, literals magenta.
func printJSON(w io.Writer, raw json.RawMessage) {
	var v any
	if json.Unmarshal(raw, &v) != nil {
		fmt.Fprintln(w, string(raw))
		return
	}
	out, _ := json.MarshalIndent(v, "", "  ")
	if !useColor || w != os.Stdout {
		fmt.Fprintln(w, string(out))
		return
	}
	for _, line := range strings.Split(string(out), "\n") {
		if m := jsonKeyRe.FindStringSubmatch(line); m != nil {
			fmt.Fprintln(w, m[1]+cyan(m[2])+m[3]+colorJSONValue(m[4]))
		} else {
			fmt.Fprintln(w, m0(line))
		}
	}
}

// m0 colours a bare array element / bracket line.
func m0(line string) string {
	indent := line[:len(line)-len(strings.TrimLeft(line, " "))]
	return indent + colorJSONValue(strings.TrimLeft(line, " "))
}
