package client

import (
	"encoding/json"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// startMockServer creates a Unix socket server that accepts one connection
// and calls handler with it. Returns the socket path and a cleanup function.
func startMockServer(t *testing.T, handler func(net.Conn)) (string, func()) {
	t.Helper()
	dir := t.TempDir()
	sockPath := filepath.Join(dir, "test.sock")

	ln, err := net.Listen("unix", sockPath)
	if err != nil {
		t.Fatalf("failed to listen: %v", err)
	}

	done := make(chan struct{})
	go func() {
		defer close(done)
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		defer conn.Close()
		handler(conn)
	}()

	cleanup := func() {
		ln.Close()
		<-done
	}
	return sockPath, cleanup
}

func TestNew_StoresConfig(t *testing.T) {
	c := New("/tmp/test.sock", 3*time.Second)
	if c.socketPath != "/tmp/test.sock" {
		t.Errorf("expected socketPath /tmp/test.sock, got %s", c.socketPath)
	}
	if c.timeout != 3*time.Second {
		t.Errorf("expected timeout 3s, got %v", c.timeout)
	}
	if c.conn != nil {
		t.Error("expected conn to be nil before Connect()")
	}
}

func TestNew_DefaultTimeout(t *testing.T) {
	c := New("/tmp/test.sock", 0)
	if c.timeout != DefaultTimeout {
		t.Errorf("expected default timeout %v, got %v", DefaultTimeout, c.timeout)
	}
}

func TestConnect_NonExistentSocket(t *testing.T) {
	c := New("/tmp/nonexistent_socket_path_12345.sock", DefaultTimeout)
	err := c.Connect()
	if err == nil {
		t.Fatal("expected error for non-existent socket")
	}
}

func TestConnect_ValidSocket(t *testing.T) {
	sockPath, cleanup := startMockServer(t, func(conn net.Conn) {
		// Just accept and hold the connection open briefly.
		buf := make([]byte, 1024)
		conn.Read(buf)
	})
	defer cleanup()

	c := New(sockPath, DefaultTimeout)
	err := c.Connect()
	if err != nil {
		t.Fatalf("expected no error, got: %v", err)
	}
	defer c.Close()
}

func TestClose_NilConn(t *testing.T) {
	c := New("/tmp/test.sock", DefaultTimeout)
	err := c.Close()
	if err != nil {
		t.Fatalf("expected no error closing nil conn, got: %v", err)
	}
}

func TestSend_NotConnected(t *testing.T) {
	c := New("/tmp/test.sock", DefaultTimeout)
	_, err := c.Send("status")
	if err == nil {
		t.Fatal("expected error when not connected")
	}
}

func TestSend_WritesCorrectJSON(t *testing.T) {
	sockPath, cleanup := startMockServer(t, func(conn net.Conn) {
		buf := make([]byte, 1024)
		n, _ := conn.Read(buf)
		received := string(buf[:n])

		// Verify the received command is valid JSON with the right structure.
		var req struct {
			Command string `json:"command"`
		}
		if err := json.Unmarshal([]byte(received[:len(received)-1]), &req); err != nil {
			return
		}

		// Send a response back.
		resp := `{"status":"success","result":{"ok":true}}` + "\n"
		conn.Write([]byte(resp))
	})
	defer cleanup()

	c := New(sockPath, DefaultTimeout)
	if err := c.Connect(); err != nil {
		t.Fatalf("connect failed: %v", err)
	}
	defer c.Close()

	resp, err := c.Send("status")
	if err != nil {
		t.Fatalf("send failed: %v", err)
	}
	if !resp.IsSuccess() {
		t.Error("expected success response")
	}
}

func TestSend_ErrorResponse(t *testing.T) {
	sockPath, cleanup := startMockServer(t, func(conn net.Conn) {
		buf := make([]byte, 1024)
		conn.Read(buf)
		resp := `{"status":"error","error":"something went wrong"}` + "\n"
		conn.Write([]byte(resp))
	})
	defer cleanup()

	c := New(sockPath, DefaultTimeout)
	if err := c.Connect(); err != nil {
		t.Fatalf("connect failed: %v", err)
	}
	defer c.Close()

	resp, err := c.Send("bad_command")
	if err != nil {
		t.Fatalf("send failed: %v", err)
	}
	if resp.IsSuccess() {
		t.Error("expected non-success response")
	}
	if resp.Error != "something went wrong" {
		t.Errorf("expected error message 'something went wrong', got '%s'", resp.Error)
	}
}

func TestSend_RawMessageResult(t *testing.T) {
	sockPath, cleanup := startMockServer(t, func(conn net.Conn) {
		buf := make([]byte, 1024)
		conn.Read(buf)
		resp := `{"status":"success","result":{"main_lcore":0,"num_pmd_threads":4}}` + "\n"
		conn.Write([]byte(resp))
	})
	defer cleanup()

	c := New(sockPath, DefaultTimeout)
	if err := c.Connect(); err != nil {
		t.Fatalf("connect failed: %v", err)
	}
	defer c.Close()

	resp, err := c.Send("status")
	if err != nil {
		t.Fatalf("send failed: %v", err)
	}

	// Verify Result is valid json.RawMessage that can be further parsed.
	var result map[string]interface{}
	if err := json.Unmarshal(resp.Result, &result); err != nil {
		t.Fatalf("failed to unmarshal result: %v", err)
	}
	if result["main_lcore"] != float64(0) {
		t.Errorf("expected main_lcore 0, got %v", result["main_lcore"])
	}
}

func TestSend_Timeout(t *testing.T) {
	sockPath, cleanup := startMockServer(t, func(conn net.Conn) {
		buf := make([]byte, 1024)
		conn.Read(buf)
		// Don't respond — let the client time out.
		time.Sleep(3 * time.Second)
	})
	defer cleanup()

	c := New(sockPath, 500*time.Millisecond)
	if err := c.Connect(); err != nil {
		t.Fatalf("connect failed: %v", err)
	}
	defer c.Close()

	_, err := c.Send("status")
	if err == nil {
		t.Fatal("expected timeout error")
	}
}

func TestIsSuccess(t *testing.T) {
	tests := []struct {
		status string
		want   bool
	}{
		{"success", true},
		{"error", false},
		{"", false},
		{"SUCCESS", false},
	}
	for _, tt := range tests {
		r := &Response{Status: tt.status}
		if got := r.IsSuccess(); got != tt.want {
			t.Errorf("IsSuccess() for status %q = %v, want %v", tt.status, got, tt.want)
		}
	}
}

func TestConnect_SocketFileExistsButNotSocket(t *testing.T) {
	// Create a regular file (not a socket) and try to connect to it.
	dir := t.TempDir()
	fakeSock := filepath.Join(dir, "fake.sock")
	if err := os.WriteFile(fakeSock, []byte("not a socket"), 0644); err != nil {
		t.Fatalf("failed to create fake socket file: %v", err)
	}

	c := New(fakeSock, DefaultTimeout)
	err := c.Connect()
	if err == nil {
		t.Fatal("expected error connecting to a regular file")
	}
}

func TestSend_ShutdownEOFAsSuccess(t *testing.T) {
	// Mock server accepts connection, reads the command, then closes
	// the connection without sending a response. The client should
	// treat this as success for the "shutdown" command (Requirement 6.3).
	sockPath, cleanup := startMockServer(t, func(conn net.Conn) {
		buf := make([]byte, 1024)
		conn.Read(buf)
		// Close without responding — simulates server shutting down.
		conn.Close()
	})
	defer cleanup()

	c := New(sockPath, DefaultTimeout)
	if err := c.Connect(); err != nil {
		t.Fatalf("connect failed: %v", err)
	}
	defer c.Close()

	resp, err := c.Send("shutdown")
	if err != nil {
		t.Fatalf("expected no error for shutdown EOF, got: %v", err)
	}
	if !resp.IsSuccess() {
		t.Errorf("expected success response for shutdown EOF, got status: %s", resp.Status)
	}
}

func TestSend_NonShutdownEOFIsError(t *testing.T) {
	// For non-shutdown commands, EOF should still be an error.
	sockPath, cleanup := startMockServer(t, func(conn net.Conn) {
		buf := make([]byte, 1024)
		conn.Read(buf)
		// Close without responding.
		conn.Close()
	})
	defer cleanup()

	c := New(sockPath, DefaultTimeout)
	if err := c.Connect(); err != nil {
		t.Fatalf("connect failed: %v", err)
	}
	defer c.Close()

	_, err := c.Send("status")
	if err == nil {
		t.Fatal("expected error for non-shutdown EOF")
	}
}
