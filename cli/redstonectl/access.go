package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"sort"
	"strings"
)

func runWhoAmI(env *Env, _ string, _ []string) error {
	raw, err := env.call("whoami", nil)
	if err != nil {
		return err
	}
	env.show(raw, func() {
		var w whoAmI
		_ = json.Unmarshal(raw, &w)
		printBox(env.Out, T("Tài khoản"), []row{
			{T("Tên"), cyan(w.User)},
			{T("Vai trò"), roleBadge(w.Role)},
			{T("Phạm vi"), scopeText(w)},
			{T("Lệnh được phép"), fmt.Sprintf("%d  %s", len(w.Commands), dim("(redstonectl perms)"))},
		})
		fmt.Fprintln(env.Out)
		fmt.Fprintf(env.Out, "  %s %s\n", dim(T("Được phép:")), cyan(strings.Join(w.Commands, " ")))
	})
	return nil
}

// runPerms prints the role x command matrix (as the app defines it) and marks the caller's column.
func runPerms(env *Env, _ string, _ []string) error {
	raw, err := env.call("whoami", nil)
	if err != nil {
		return err
	}
	var w struct {
		Role   string              `json:"role"`
		Matrix map[string][]string `json:"matrix"`
	}
	_ = json.Unmarshal(raw, &w)
	env.show(raw, func() {
		roles := []string{"viewer", "operator", "admin"}
		has := map[string]map[string]bool{}
		all := map[string]bool{}
		for _, r := range roles {
			has[r] = map[string]bool{}
			for _, c := range w.Matrix[r] {
				has[r][c] = true
				all[c] = true
			}
		}
		names := make([]string, 0, len(all))
		for c := range all {
			names = append(names, c)
		}
		// group by the lowest role that may run it, then alphabetically
		rank := func(c string) int {
			for i, r := range roles {
				if has[r][c] {
					return i
				}
			}
			return len(roles)
		}
		sort.Slice(names, func(i, j int) bool {
			if rank(names[i]) != rank(names[j]) {
				return rank(names[i]) < rank(names[j])
			}
			return names[i] < names[j]
		})

		headers := []string{T("Lệnh")}
		for _, r := range roles {
			label := r
			if r == w.Role {
				label = r + " ◀"
			}
			headers = append(headers, label)
		}
		rows := make([][]string, 0, len(names))
		for _, c := range names {
			name := dim(c)
			if has[w.Role][c] {
				name = bold(c)
			}
			row := []string{name}
			for _, r := range roles {
				if has[r][c] {
					row = append(row, green("✓"))
				} else {
					row = append(row, dim("·"))
				}
			}
			rows = append(rows, row)
		}
		fmt.Fprintf(env.Out, "%s %s\n\n", dim(T("Bạn đang là")), roleBadge(w.Role))
		printTable(env.Out, headers, rows)
		fmt.Fprintf(env.Out, "\n  %s\n", dim(T("In đậm = bạn được phép chạy. Mỗi vai trò gồm cả quyền của vai trò thấp hơn.")))
	})
	return nil
}

func runAudit(env *Env, _ string, args []string) error {
	fs := newFlagSet("audit")
	n := fs.Int("n", 50, "")
	if _, err := parseInterspersed(fs, args); err != nil {
		return fmt.Errorf("%w: %v", errUsage, err)
	}
	raw, err := env.call("audit", map[string]any{"lines": *n})
	if err != nil {
		return err
	}
	env.show(raw, func() {
		var r struct {
			Lines []string `json:"lines"`
		}
		_ = json.Unmarshal(raw, &r)
		if len(r.Lines) == 0 {
			fmt.Fprintln(env.Out, dim(T("(chưa có hoạt động nào)")))
			return
		}
		for _, l := range r.Lines {
			parts := strings.SplitN(l, " | ", 4)
			if len(parts) < 4 {
				fmt.Fprintln(env.Out, l)
				continue
			}
			outcome := parts[3]
			switch {
			case strings.HasPrefix(outcome, "DENIED"):
				outcome = red(outcome)
			case strings.HasPrefix(outcome, "error"):
				outcome = yellow(outcome)
			default:
				outcome = green(outcome)
			}
			fmt.Fprintf(env.Out, "%s  %s  %s  %s\n", dim(parts[0]), cyan(parts[1]), parts[2], outcome)
		}
	})
	return nil
}

var _ = flag.ContinueOnError
