package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"net"
	"regexp"
	"strings"
	"testing"
	"time"
)

// Vector produced independently with Python's hmac module; the C++ side
// (QMessageAuthenticationCode) is checked against the same algorithm by the
// end-to-end run documented in cli/README.md.
func TestComputeProofVector(t *testing.T) {
	got := ComputeProof("0123456789abcdef0123456789abcdef", "00112233445566778899aabbccddeeff")
	want := "e2f18cb41fd0132236a07d04a640d4835d49aa14a1e3cadc7a6af79b13bd33bb"
	if got != want {
		t.Fatalf("proof = %s, want %s", got, want)
	}
}

func TestSplitArgs(t *testing.T) {
	cases := map[string][]string{
		`say "hello world" x`: {"say", "hello world", "x"},
		`msg Steve 'a  b'`:    {"msg", "Steve", "a  b"},
		`  a   b  `:           {"a", "b"},
		`op ""`:               {"op", ""},
	}
	for in, want := range cases {
		got, err := splitArgs(in)
		if err != nil || strings.Join(got, "|") != strings.Join(want, "|") {
			t.Errorf("splitArgs(%q) = %v, %v; want %v", in, got, err, want)
		}
	}
	if _, err := splitArgs(`say "oops`); err == nil {
		t.Error("expected error for unterminated quote")
	}
}

// fakeApp is a minimal in-process implementation of the server side of the protocol.
func fakeApp(t *testing.T, token string) (addr string, stop func()) {
	t.Helper()
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	go func() {
		for {
			c, err := l.Accept()
			if err != nil {
				return
			}
			go func(c net.Conn) {
				defer c.Close()
				const nonce = "abcdef0123456789abcdef0123456789"
				c.Write([]byte(`{"type":"hello","protocol":1,"nonce":"` + nonce + `"}` + "\n"))
				r := bufio.NewReader(c)
				line, _ := r.ReadBytes('\n')
				var a struct{ Proof string }
				json.Unmarshal(line, &a)
				if a.Proof != ComputeProof(token, nonce) {
					c.Write([]byte(`{"type":"auth","ok":false,"error":"Sai token."}` + "\n"))
					return
				}
				c.Write([]byte(`{"type":"auth","ok":true}` + "\n"))
				for {
					line, err := r.ReadBytes('\n')
					if err != nil {
						return
					}
					var req map[string]any
					json.Unmarshal(line, &req)
					id := int64(req["id"].(float64))
					switch req["cmd"] {
					case "ping":
						resp, _ := json.Marshal(map[string]any{"id": id, "ok": true, "data": map[string]any{"pong": true}})
						c.Write(append(resp, '\n'))
					default:
						resp, _ := json.Marshal(map[string]any{"id": id, "ok": false, "error": "không hỗ trợ"})
						c.Write(append(resp, '\n'))
					}
				}
			}(c)
		}
	}()
	return l.Addr().String(), func() { l.Close() }
}

func TestHandshakeAndCall(t *testing.T) {
	addr, stop := fakeApp(t, "secret-token-secret-token")
	defer stop()

	c, err := Dial(addr, "secret-token-secret-token", 2*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	raw, err := c.Call("ping", nil, 2*time.Second)
	if err != nil || !strings.Contains(string(raw), "pong") {
		t.Fatalf("ping: %s %v", raw, err)
	}
	_, err = c.Call("whatever", nil, 2*time.Second)
	var re *RemoteError
	if err == nil || !errorAs(err, &re) {
		t.Fatalf("expected RemoteError, got %v", err)
	}
}

func errorAs(err error, target **RemoteError) bool {
	re, ok := err.(*RemoteError)
	if ok {
		*target = re
	}
	return ok
}

func TestWrongTokenRejected(t *testing.T) {
	addr, stop := fakeApp(t, "right-token-right-token")
	defer stop()
	if _, err := Dial(addr, "wrong-token-wrong-token", 2*time.Second); err == nil || !strings.Contains(err.Error(), "xác thực") {
		t.Fatalf("expected auth failure, got %v", err)
	}
}

func TestEnglishDictionaryPlaceholders(t *testing.T) {
	ph := regexp.MustCompile(`%[-+# 0]*\d*(?:\.\d+)?[a-zA-Z]`)
	for vi, en := range enDict {
		if en == "" {
			t.Errorf("empty translation for %q", vi)
		}
		a, b := ph.FindAllString(vi, -1), ph.FindAllString(en, -1)
		if strings.Join(a, "|") != strings.Join(b, "|") {
			t.Errorf("placeholder mismatch: %q -> %q", vi, en)
		}
	}
}

func TestTranslateSwitchesLanguage(t *testing.T) {
	defer func(old string) { lang = old }(lang)
	lang = "en"
	if got := T("Cấp OP"); got != "Give OP" {
		t.Fatalf("en: %q", got)
	}
	lang = "vi"
	if got := T("Cấp OP"); got != "Cấp OP" {
		t.Fatalf("vi: %q", got)
	}
	if detectLang("vi_VN.UTF-8") != "vi" || detectLang("en") != "en" || detectLang("fr") != "en" {
		t.Fatal("detectLang")
	}
}

func TestBannerShape(t *testing.T) {
	var buf bytes.Buffer
	old := useColor
	useColor = false
	defer func() { useColor = old }()
	printBanner(&buf)
	lines := strings.Split(strings.TrimRight(buf.String(), "\n"), "\n")
	if len(lines) < len(bannerChars) {
		t.Fatalf("banner has %d lines", len(lines))
	}
	if strings.Contains(buf.String(), "\x1b[") {
		t.Fatal("colour codes leaked with colour off")
	}
}
