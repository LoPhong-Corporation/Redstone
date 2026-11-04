//go:build !windows

package main

func osProtect(plain string) (string, bool) { return "", false }

func osUnprotect(string) (string, error) { return "", errCannotDecrypt }
