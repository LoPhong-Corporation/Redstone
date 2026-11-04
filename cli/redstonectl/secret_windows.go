//go:build windows

package main

import (
	"encoding/base64"
	"syscall"
	"unsafe"
)

type dataBlob struct {
	cbData uint32
	pbData *byte
}

var (
	crypt32          = syscall.NewLazyDLL("crypt32.dll")
	kernel32Secret   = syscall.NewLazyDLL("kernel32.dll")
	procProtectData  = crypt32.NewProc("CryptProtectData")
	procUnprotect    = crypt32.NewProc("CryptUnprotectData")
	procLocalFree    = kernel32Secret.NewProc("LocalFree")
	secretEntropy    = []byte("redstonectl-profile")
	cryptUIForbidden = uintptr(0x1) // CRYPTPROTECT_UI_FORBIDDEN
)

func blobOf(b []byte) dataBlob {
	if len(b) == 0 {
		return dataBlob{}
	}
	return dataBlob{cbData: uint32(len(b)), pbData: &b[0]}
}

func takeBlob(out dataBlob) []byte {
	data := make([]byte, out.cbData)
	if out.cbData > 0 {
		copy(data, unsafe.Slice(out.pbData, out.cbData))
	}
	procLocalFree.Call(uintptr(unsafe.Pointer(out.pbData)))
	return data
}

// osProtect encrypts with DPAPI (current-user scope) and returns base64.
func osProtect(plain string) (string, bool) {
	in := blobOf([]byte(plain))
	entropy := blobOf(secretEntropy)
	var out dataBlob
	r, _, _ := procProtectData.Call(
		uintptr(unsafe.Pointer(&in)), 0, uintptr(unsafe.Pointer(&entropy)), 0, 0, cryptUIForbidden, uintptr(unsafe.Pointer(&out)))
	if r == 0 {
		return "", false
	}
	return base64.StdEncoding.EncodeToString(takeBlob(out)), true
}

func osUnprotect(b64 string) (string, error) {
	cipher, err := base64.StdEncoding.DecodeString(b64)
	if err != nil {
		return "", errCannotDecrypt
	}
	in := blobOf(cipher)
	entropy := blobOf(secretEntropy)
	var out dataBlob
	r, _, _ := procUnprotect.Call(
		uintptr(unsafe.Pointer(&in)), 0, uintptr(unsafe.Pointer(&entropy)), 0, 0, cryptUIForbidden, uintptr(unsafe.Pointer(&out)))
	if r == 0 {
		return "", errCannotDecrypt
	}
	return string(takeBlob(out)), nil
}
