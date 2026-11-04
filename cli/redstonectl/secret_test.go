package main

import "testing"

func TestSecretPassThrough(t *testing.T) {
	if got, err := unprotectSecret("plain-token"); err != nil || got != "plain-token" {
		t.Fatalf("plain values must pass through: %q %v", got, err)
	}
	if protectSecret("") != "" {
		t.Fatal("empty stays empty")
	}
}

func TestSecretRoundTrip(t *testing.T) {
	stored := protectSecret("0123456789abcdef-secret")
	got, err := unprotectSecret(stored)
	if err != nil || got != "0123456789abcdef-secret" {
		t.Fatalf("round trip failed: %q %v (stored %q)", got, err, stored)
	}
	if isProtected(stored) && stored == "0123456789abcdef-secret" {
		t.Fatal("a protected value must not equal the plain text")
	}
}

func TestSecretGarbageIsRejected(t *testing.T) {
	if _, err := unprotectSecret(dpapiPrefix + "not-a-valid-blob"); err == nil {
		t.Fatal("garbage dpapi value must not decrypt")
	}
}
