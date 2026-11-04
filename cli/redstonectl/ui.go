package main

import (
	"fmt"
	"io"
	"os"
	"regexp"
	"strings"
	"sync"
	"time"
	"unicode/utf8"
)

var ansiRe = regexp.MustCompile("\x1b\\[[0-9;]*m")

// visibleLen counts the characters a line occupies on screen (escape codes excluded).
func visibleLen(s string) int { return utf8.RuneCountInString(ansiRe.ReplaceAllString(s, "")) }

func padRight(s string, width int) string {
	if n := visibleLen(s); n < width {
		return s + strings.Repeat(" ", width-n)
	}
	return s
}

// row is one "label  value" line of a box.
type row struct{ key, value string }

// printBox draws a titled box with aligned label/value rows:
//
//	╭─ Title ────────────╮
//	│ Label    value     │
//	╰────────────────────╯
func printBox(w io.Writer, title string, rows []row) {
	keyWidth := 0
	for _, r := range rows {
		if n := visibleLen(r.key); n > keyWidth {
			keyWidth = n
		}
	}
	lines := make([]string, 0, len(rows))
	inner := visibleLen(title) + 4
	for _, r := range rows {
		line := padRight(dim(r.key), keyWidth) + "  " + r.value
		if r.key == "" {
			line = r.value
		}
		lines = append(lines, line)
		if n := visibleLen(line); n > inner {
			inner = n
		}
	}
	if inner < 46 {
		inner = 46
	}
	border := func(s string) string { return paint("38;5;131", s) } // muted red
	fmt.Fprintf(w, "%s %s %s\n", border("╭─"), heading(title), border(strings.Repeat("─", inner-visibleLen(title)-1)+"╮"))
	for _, l := range lines {
		fmt.Fprintf(w, "%s %s %s\n", border("│"), padRight(l, inner), border("│"))
	}
	fmt.Fprintln(w, border("╰"+strings.Repeat("─", inner+2)+"╯"))
}

// stateBadge: coloured dot + state name.
func stateBadge(state string) string {
	switch state {
	case "Running":
		return green("●") + " " + green(state)
	case "Crashed":
		return red("●") + " " + red(state)
	case "Stopped":
		return dim("●") + " " + dim(state)
	default:
		return yellow("●") + " " + yellow(state)
	}
}

func roleBadge(role string) string {
	switch role {
	case "admin":
		return paint("1;41;97", " admin ")
	case "operator":
		return paint("1;43;30", " operator ")
	default:
		return paint("1;42;30", " viewer ")
	}
}

func roleText(role string) string {
	switch role {
	case "admin":
		return red(role)
	case "operator":
		return yellow(role)
	}
	return green(role)
}

func yesNoColor(b bool) string {
	if b {
		return green(T("bật"))
	}
	return dim(T("tắt"))
}

func check(ok bool) string {
	if ok {
		return green("✓")
	}
	return red("✗")
}

// ------------------------------------------------------------------ spinner

// spinner shows "⠋ message" on stderr while something slow runs. It only animates on
// a real terminal; otherwise it prints the message once so logs stay readable.
type spinner struct {
	mu      sync.Mutex
	msg     string
	stop    chan struct{}
	done    chan struct{}
	animate bool
}

var spinnerFrames = []string{"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"}

func startSpinner(msg string) *spinner {
	s := &spinner{msg: msg, stop: make(chan struct{}), done: make(chan struct{}), animate: useColorErr}
	if !s.animate {
		printInfoErr(msg)
		close(s.done)
		return s
	}
	go func() {
		defer close(s.done)
		started := time.Now()
		t := time.NewTicker(90 * time.Millisecond)
		defer t.Stop()
		for i := 0; ; i++ {
			s.mu.Lock()
			m := s.msg
			s.mu.Unlock()
			fmt.Fprintf(os.Stderr, "\r\x1b[K%s %s %s", paint("36", spinnerFrames[i%len(spinnerFrames)]), m, dim(fmt.Sprintf("(%ds)", int(time.Since(started).Seconds()))))
			select {
			case <-s.stop:
				fmt.Fprint(os.Stderr, "\r\x1b[K")
				return
			case <-t.C:
			}
		}
	}()
	return s
}

func (s *spinner) Update(msg string) {
	s.mu.Lock()
	s.msg = msg
	s.mu.Unlock()
}

func (s *spinner) Stop() {
	select {
	case <-s.stop:
	default:
		close(s.stop)
	}
	<-s.done
}

// printTable prints a table whose cells may contain colour codes (tabwriter cannot align those).
func printTable(w io.Writer, headers []string, rows [][]string) {
	widths := make([]int, len(headers))
	for i, h := range headers {
		widths[i] = visibleLen(h)
	}
	for _, r := range rows {
		for i, c := range r {
			if n := visibleLen(c); n > widths[i] {
				widths[i] = n
			}
		}
	}
	line := func(cells []string, styled bool) string {
		parts := make([]string, len(cells))
		for i, c := range cells {
			if styled {
				c = bold(paint("91", c))
			}
			parts[i] = padRight(c, widths[i])
		}
		return "  " + strings.Join(parts, "  ")
	}
	fmt.Fprintln(w, line(headers, true))
	total := 2
	for _, x := range widths {
		total += x + 2
	}
	fmt.Fprintln(w, "  "+dim(strings.Repeat("─", total-4)))
	for _, r := range rows {
		fmt.Fprintln(w, line(r, false))
	}
}
