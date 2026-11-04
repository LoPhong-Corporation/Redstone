package main

import (
	"errors"
	"strings"
)

// Tokens saved in the profile file are protected with the OS when possible:
// Windows -> DPAPI (only the same Windows account on the same PC can read them back).
// Other systems store them as is (the file is mode 0600). Stored form: "dpapi:<base64>".
const dpapiPrefix = "dpapi:"

var errCannotDecrypt = errors.New("cannot decrypt stored token")

func isProtected(stored string) bool { return strings.HasPrefix(stored, dpapiPrefix) }

// unprotectSecret returns the plain token for a stored value (plain values pass through).
func unprotectSecret(stored string) (string, error) {
	if !isProtected(stored) {
		return stored, nil
	}
	return osUnprotect(strings.TrimPrefix(stored, dpapiPrefix))
}

// protectSecret returns what should be written to disk.
func protectSecret(plain string) string {
	if plain == "" {
		return plain
	}
	if enc, ok := osProtect(plain); ok {
		return dpapiPrefix + enc
	}
	return plain
}
