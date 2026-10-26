package main

import (
	"bufio"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"sync"
	"time"
)

// Protocol: newline-delimited JSON over TCP (see docs/REMOTE_PROTOCOL.md).
//
//	S -> {"type":"hello","protocol":1,"nonce":"<hex>"}
//	C -> {"cmd":"auth","proof":"<hex HMAC-SHA256(token, nonce)>"}
//	S -> {"type":"auth","ok":true}
//	C -> {"id":1,"cmd":"list"}            S -> {"id":1,"ok":true,"data":{...}}
//	S -> {"type":"event","event":"console","server":"<id>","line":"..."}   (after subscribe)
const protocolVersion = 1

// ComputeProof returns hex(HMAC-SHA256(key=token, msg=nonce)) - the token itself is never sent.
func ComputeProof(token, nonce string) string {
	mac := hmac.New(sha256.New, []byte(token))
	mac.Write([]byte(nonce))
	return hex.EncodeToString(mac.Sum(nil))
}

// Event is a server-pushed message (console line or state change).
type Event struct {
	Event  string `json:"event"`
	Server string `json:"server"`
	Line   string `json:"line"`
	State  string `json:"state"`
}

type response struct {
	ID    int64           `json:"id"`
	OK    bool            `json:"ok"`
	Data  json.RawMessage `json:"data"`
	Error string          `json:"error"`
}

// RemoteError is an error reported by the app (as opposed to a transport failure).
type RemoteError struct{ Message string }

func (e *RemoteError) Error() string { return e.Message }

type Client struct {
	conn    net.Conn
	w       *bufio.Writer
	wmu     sync.Mutex
	mu      sync.Mutex
	nextID  int64
	pending map[int64]chan response
	closed  bool
	readErr error
	Events  chan Event
}

// Dial connects, performs the HMAC handshake and starts the reader loop.
func Dial(addr, token string, timeout time.Duration) (*Client, error) {
	conn, err := net.DialTimeout("tcp", addr, timeout)
	if err != nil {
		return nil, fmt.Errorf("không kết nối được tới %s: %w (app Redstone đã bật \"Điều khiển từ xa\" chưa?)", addr, err)
	}
	r := bufio.NewReaderSize(conn, 1<<20)

	_ = conn.SetDeadline(time.Now().Add(timeout))
	line, err := r.ReadBytes('\n')
	if err != nil {
		conn.Close()
		return nil, fmt.Errorf("app đóng kết nối ngay khi vừa mở (%v) - có thể đang bị khoá 30 giây sau nhiều lần sai token, hoặc đã đủ số phiên tối đa, hoặc port này không phải Redstone", err)
	}
	var hello struct {
		Type     string `json:"type"`
		Protocol int    `json:"protocol"`
		Nonce    string `json:"nonce"`
	}
	if err := json.Unmarshal(line, &hello); err != nil || hello.Type != "hello" || hello.Nonce == "" {
		conn.Close()
		return nil, errors.New("server không phải Redstone hoặc gửi lời chào không hợp lệ")
	}
	if hello.Protocol != protocolVersion {
		conn.Close()
		return nil, fmt.Errorf("khác phiên bản giao thức (app: %d, CLI: %d) - hãy cập nhật cả hai", hello.Protocol, protocolVersion)
	}

	auth, _ := json.Marshal(map[string]string{"cmd": "auth", "proof": ComputeProof(token, hello.Nonce)})
	if _, err := conn.Write(append(auth, '\n')); err != nil {
		conn.Close()
		return nil, err
	}
	line, err = r.ReadBytes('\n')
	if err != nil {
		conn.Close()
		return nil, fmt.Errorf("xác thực thất bại: %w", err)
	}
	var ar struct {
		OK    bool   `json:"ok"`
		Error string `json:"error"`
	}
	if err := json.Unmarshal(line, &ar); err != nil || !ar.OK {
		conn.Close()
		msg := ar.Error
		if msg == "" {
			msg = "bị từ chối"
		}
		return nil, fmt.Errorf("xác thực thất bại: %s", msg)
	}
	_ = conn.SetDeadline(time.Time{})

	c := &Client{
		conn:    conn,
		w:       bufio.NewWriter(conn),
		pending: map[int64]chan response{},
		Events:  make(chan Event, 1024),
	}
	go c.readLoop(r)
	return c, nil
}

func (c *Client) readLoop(r *bufio.Reader) {
	for {
		line, err := r.ReadBytes('\n')
		if err != nil {
			c.fail(err)
			return
		}
		var probe struct {
			Type string `json:"type"`
		}
		if json.Unmarshal(line, &probe) != nil {
			continue
		}
		if probe.Type == "event" {
			var ev Event
			if json.Unmarshal(line, &ev) == nil {
				select {
				case c.Events <- ev:
				default: // slow consumer: drop rather than block the protocol
				}
			}
			continue
		}
		var resp response
		if json.Unmarshal(line, &resp) != nil {
			continue
		}
		c.mu.Lock()
		ch := c.pending[resp.ID]
		delete(c.pending, resp.ID)
		c.mu.Unlock()
		if ch != nil {
			ch <- resp
		}
	}
}

func (c *Client) fail(err error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.closed {
		return
	}
	c.closed = true
	c.readErr = err
	for id, ch := range c.pending {
		close(ch)
		delete(c.pending, id)
	}
	close(c.Events)
}

func (c *Client) Close() error { return c.conn.Close() }

// Call sends one request and waits for its response (or the timeout).
func (c *Client) Call(cmd string, params map[string]any, timeout time.Duration) (json.RawMessage, error) {
	req := map[string]any{"cmd": cmd}
	for k, v := range params {
		req[k] = v
	}

	c.mu.Lock()
	if c.closed {
		c.mu.Unlock()
		return nil, errors.New("kết nối đã đóng")
	}
	c.nextID++
	id := c.nextID
	ch := make(chan response, 1)
	c.pending[id] = ch
	c.mu.Unlock()
	req["id"] = id

	data, err := json.Marshal(req)
	if err != nil {
		return nil, err
	}
	c.wmu.Lock()
	_, err = c.w.Write(append(data, '\n'))
	if err == nil {
		err = c.w.Flush()
	}
	c.wmu.Unlock()
	if err != nil {
		return nil, err
	}

	select {
	case resp, ok := <-ch:
		if !ok {
			return nil, errors.New("mất kết nối tới app")
		}
		if !resp.OK {
			return nil, &RemoteError{Message: resp.Error}
		}
		return resp.Data, nil
	case <-time.After(timeout):
		c.mu.Lock()
		delete(c.pending, id)
		c.mu.Unlock()
		return nil, fmt.Errorf("quá thời gian chờ phản hồi cho lệnh %q (%s)", cmd, timeout)
	}
}
