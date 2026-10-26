//go:build windows

package main

import (
	"os"
	"syscall"
	"unsafe"
)

// enableVirtualTerminal turns on ANSI escape handling for the Windows console
// (Windows 10+). Returns false if the console does not support it.
func enableVirtualTerminal() bool {
	kernel32 := syscall.NewLazyDLL("kernel32.dll")
	getMode := kernel32.NewProc("GetConsoleMode")
	setMode := kernel32.NewProc("SetConsoleMode")
	handle := os.Stdout.Fd()
	var mode uint32
	if r, _, _ := getMode.Call(handle, uintptr(unsafe.Pointer(&mode))); r == 0 {
		return false
	}
	const enableVT = 0x0004
	if r, _, _ := setMode.Call(handle, uintptr(mode|enableVT)); r == 0 {
		return false
	}
	return true
}
