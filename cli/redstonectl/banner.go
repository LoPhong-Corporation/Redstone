package main

import (
	"fmt"
	"io"
	"strings"
)

// printBanner draws the redstone-dust icon as ASCII art next to the program name.
// With colour on, each glyph takes one of six 256-colour reds (dark -> bright).
func printBanner(w io.Writer) {
	right := map[int][]string{
		3: {bold(heading("R E D S T O N E")) + "  " + dim("CLI")},
		4: {T("Điều khiển từ xa Minecraft Server Manager")},
		5: {dim("redstonectl " + version)},
		7: {cyan("redstonectl help") + dim("  "+T("xem danh sách lệnh"))},
	}
	for row := range bannerChars {
		var b strings.Builder
		b.WriteString("  ")
		chars := []rune(bannerChars[row])
		shades := []rune(bannerShade[row])
		last := -2
		for i, ch := range chars {
			s := shades[i]
			if s == '.' || ch == ' ' {
				if useColor && last != -1 {
					b.WriteString("\x1b[0m")
					last = -1
				}
				b.WriteRune(' ')
				continue
			}
			idx := int(s - '0')
			if useColor && idx != last {
				fmt.Fprintf(&b, "\x1b[38;5;%dm", bannerPalette[idx])
				last = idx
			}
			b.WriteRune(ch)
		}
		if useColor {
			b.WriteString("\x1b[0m")
		}
		line := b.String()
		if extra, ok := right[row]; ok {
			line += "   " + strings.Join(extra, " ")
		}
		fmt.Fprintln(w, line)
	}
	fmt.Fprintln(w)
}
