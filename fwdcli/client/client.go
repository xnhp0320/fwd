package client

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"time"
)

// DefaultTimeout is the default read/write deadline for socket operations.
const DefaultTimeout = 5 * time.Second

// Response represents a parsed JSON response from the control plane.
type Response struct {
	Status string          `json:"status"`
	Result json.RawMessage `json:"result"`
	Error  string          `json:"error"`
}

// IsSuccess returns true if the response status is "success".
func (r *Response) IsSuccess() bool {
	return r.Status == "success"
}

// Client communicates with the DPDK control plane over a Unix domain socket.
type Client struct {
	socketPath string
	conn       net.Conn
	timeout    time.Duration
}

// New creates a Client configured for the given socket path and timeout.
// It stores the configuration without establishing a connection.
func New(socketPath string, timeout time.Duration) *Client {
	if timeout <= 0 {
		timeout = DefaultTimeout
	}
	return &Client{
		socketPath: socketPath,
		timeout:    timeout,
	}
}

// Connect establishes the Unix domain socket connection.
// Returns an error if the socket file does not exist or the connection fails.
func (c *Client) Connect() error {
	if _, err := os.Stat(c.socketPath); os.IsNotExist(err) {
		return fmt.Errorf("socket not found: %s", c.socketPath)
	}
	conn, err := net.DialTimeout("unix", c.socketPath, c.timeout)
	if err != nil {
		return fmt.Errorf("failed to connect to %s: %w", c.socketPath, err)
	}
	c.conn = conn
	return nil
}

// Close closes the underlying socket connection.
func (c *Client) Close() error {
	if c.conn != nil {
		return c.conn.Close()
	}
	return nil
}

// Send sends a command name to the control plane and returns the parsed response.
// It writes {"command":"<name>"}\n and reads until the next newline.
func (c *Client) Send(command string) (*Response, error) {
	if c.conn == nil {
		return nil, fmt.Errorf("not connected")
	}

	// Build the request payload.
	req := struct {
		Command string `json:"command"`
	}{Command: command}

	data, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal command: %w", err)
	}
	data = append(data, '\n')

	// Write the command to the socket.
	if _, err := c.conn.Write(data); err != nil {
		return nil, fmt.Errorf("failed to write command: %w", err)
	}

	// Set read deadline for the response.
	if err := c.conn.SetReadDeadline(time.Now().Add(c.timeout)); err != nil {
		return nil, fmt.Errorf("failed to set read deadline: %w", err)
	}

	// Read until newline.
	reader := bufio.NewReader(c.conn)
	line, err := reader.ReadBytes('\n')
	if err != nil {
		// For shutdown commands, treat EOF as success — the server may close
		// the connection before sending a response (Requirement 6.3).
		if command == "shutdown" && (err == io.EOF || err == io.ErrUnexpectedEOF) {
			return &Response{Status: "success"}, nil
		}
		return nil, fmt.Errorf("failed to read response: %w", err)
	}

	// Parse the JSON response.
	var resp Response
	if err := json.Unmarshal(line, &resp); err != nil {
		return nil, fmt.Errorf("failed to parse response: %w", err)
	}

	return &resp, nil
}
