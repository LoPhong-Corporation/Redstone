package main

import (
	"fmt"
	"io"
	"os"
	"runtime"
	"strings"
)

// colourDepth: 0 = no colour, 8 = 256 colours, 24 = truecolor.
func colourDepth() int {
	if !useColor {
		return 0
	}
	ct := strings.ToLower(os.Getenv("COLORTERM"))
	if strings.Contains(ct, "truecolor") || strings.Contains(ct, "24bit") || os.Getenv("WT_SESSION") != "" ||
		os.Getenv("TERM_PROGRAM") == "vscode" || os.Getenv("TERM_PROGRAM") == "iTerm.app" {
		return 24
	}
	return 8
}

// rgbTo256 maps an RGB colour to the nearest xterm-256 colour (6x6x6 cube or the grey ramp).
func rgbTo256(r, g, b uint8) int {
	cube := func(v uint8) int {
		if v < 48 {
			return 0
		}
		if v < 115 {
			return 1
		}
		return int((int(v) - 35) / 40) // 95,135,175,215,255 -> 2..5
	}
	ci, cj, ck := cube(r), cube(g), cube(b)
	level := func(i int) int {
		if i == 0 {
			return 0
		}
		return 55 + 40*i
	}
	best := 16 + 36*ci + 6*cj + ck
	bestDist := dist(int(r), int(g), int(b), level(ci), level(cj), level(ck))
	// grey ramp 232..255 = 8 + 10*i
	avg := (int(r) + int(g) + int(b)) / 3
	gi := (avg - 8) / 10
	if gi < 0 {
		gi = 0
	}
	if gi > 23 {
		gi = 23
	}
	gv := 8 + 10*gi
	if d := dist(int(r), int(g), int(b), gv, gv, gv); d < bestDist {
		best = 232 + gi
	}
	return best
}

func dist(r1, g1, b1, r2, g2, b2 int) int {
	return (r1-r2)*(r1-r2) + (g1-g2)*(g1-g2) + (b1-b2)*(b1-b2)
}

// bgCell is one sprite pixel: two spaces painted with a BACKGROUND colour.
func bgCell(depth int, c [3]uint8) string {
	if depth >= 24 {
		return fmt.Sprintf("\x1b[48;2;%d;%d;%dm  \x1b[0m", c[0], c[1], c[2])
	}
	return fmt.Sprintf("\x1b[48;5;%dm  \x1b[0m", rgbTo256(c[0], c[1], c[2]))
}

// printBanner draws the redstone-dust sprite as solid coloured blocks (background colours,
// not coloured text) next to the program name and a few facts. Without colour it falls back
// to plain ASCII art.
func printBanner(w io.Writer) {
	depth := colourDepth()

	ssh := sshInfo()
	sshLine := red("✗ ") + dim(T("chưa cài"))
	if ssh.Found {
		sshLine = green("✓ ") + ssh.Version
	}
	right := map[int]string{
		2:  bold(heading("R E D S T O N E")) + "  " + dim("CLI"),
		3:  T("Điều khiển từ xa Minecraft Server Manager"),
		5:  dim(T("Phiên bản   ")) + "redstonectl " + cyan(version),
		6:  dim(T("Hệ thống    ")) + fmt.Sprintf("%s/%s · %s", runtime.GOOS, runtime.GOARCH, runtime.Version()),
		7:  dim(T("Giao thức   ")) + fmt.Sprintf("v%d", protocolVersion) + dim(" · ") + dim(T("ngôn ngữ: ")) + lang,
		8:  dim(T("OpenSSH     ")) + sshLine,
		10: cyan("redstonectl help") + dim("  "+T("xem danh sách lệnh")) + dim("  ·  ") + cyan("redstonectl info") + dim("  "+T("xem chi tiết kết nối")),
	}

	for y, gridRow := range bannerGrid {
		var b strings.Builder
		b.WriteString("  ")
		if depth == 0 {
			b.WriteString(bannerChars[y])
		} else {
			for _, ch := range gridRow {
				if ch == '.' {
					b.WriteString("  ")
					continue
				}
				b.WriteString(bgCell(depth, bannerPalette[int(ch-'a')]))
			}
		}
		line := b.String()
		if extra, ok := right[y]; ok {
			line += "   " + extra
		}
		fmt.Fprintln(w, line)
	}
	fmt.Fprintln(w)
}
