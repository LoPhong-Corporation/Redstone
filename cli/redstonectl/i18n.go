package main

import (
	"os"
	"strings"
)

// Languages: Vietnamese is the source language of every message in the code;
// English comes from enDict (lang_en.go). Pick with --lang, REDSTONE_LANG, or
// the system locale. T() returns the source text when no translation exists.
var lang = "vi"

func T(source string) string {
	if lang == "en" {
		if t, ok := enDict[source]; ok {
			return t
		}
	}
	return source
}

func detectLang(flagValue string) string {
	candidates := []string{flagValue, os.Getenv("REDSTONE_LANG"), os.Getenv("LC_ALL"), os.Getenv("LC_MESSAGES"), os.Getenv("LANG"), systemLocale()}
	for _, c := range candidates {
		c = strings.ToLower(strings.TrimSpace(c))
		if c == "" || c == "auto" || c == "c" || c == "posix" {
			continue
		}
		if strings.HasPrefix(c, "vi") {
			return "vi"
		}
		return "en"
	}
	return "en"
}
