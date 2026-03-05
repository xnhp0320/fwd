package formatter

import (
	"encoding/json"
	"strings"
	"testing"
	"time"
)

func TestFormatStatus(t *testing.T) {
	raw := json.RawMessage(`{"main_lcore":0,"num_pmd_threads":4,"uptime_seconds":3600}`)
	out, err := FormatStatus(raw)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !strings.Contains(out, "0") {
		t.Error("output should contain main_lcore value 0")
	}
	if !strings.Contains(out, "4") {
		t.Error("output should contain num_pmd_threads value 4")
	}
	if !strings.Contains(out, "3600") {
		t.Error("output should contain uptime_seconds value 3600")
	}
}

func TestFormatStatusInvalidJSON(t *testing.T) {
	raw := json.RawMessage(`{invalid}`)
	_, err := FormatStatus(raw)
	if err == nil {
		t.Error("expected error for invalid JSON")
	}
}

func TestFormatThreads(t *testing.T) {
	raw := json.RawMessage(`{"threads":[{"lcore_id":1},{"lcore_id":2},{"lcore_id":5}]}`)
	out, err := FormatThreads(raw)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !strings.Contains(out, "1") {
		t.Error("output should contain lcore_id 1")
	}
	if !strings.Contains(out, "2") {
		t.Error("output should contain lcore_id 2")
	}
	if !strings.Contains(out, "5") {
		t.Error("output should contain lcore_id 5")
	}
	if !strings.Contains(out, "3") {
		t.Error("output should contain thread count 3")
	}
}

func TestFormatThreadsEmpty(t *testing.T) {
	raw := json.RawMessage(`{"threads":[]}`)
	out, err := FormatThreads(raw)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !strings.Contains(out, "0") {
		t.Error("output should contain thread count 0")
	}
}

func TestFormatStats(t *testing.T) {
	raw := json.RawMessage(`{
		"threads":[
			{"lcore_id":1,"packets":1000,"bytes":64000},
			{"lcore_id":2,"packets":2000,"bytes":128000}
		],
		"total":{"packets":3000,"bytes":192000}
	}`)
	out, err := FormatStats(raw)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	// Check per-thread values
	if !strings.Contains(out, "1000") {
		t.Error("output should contain packets 1000")
	}
	if !strings.Contains(out, "64000") {
		t.Error("output should contain bytes 64000")
	}
	if !strings.Contains(out, "2000") {
		t.Error("output should contain packets 2000")
	}
	if !strings.Contains(out, "128000") {
		t.Error("output should contain bytes 128000")
	}
	// Check totals
	if !strings.Contains(out, "3000") {
		t.Error("output should contain total packets 3000")
	}
	if !strings.Contains(out, "192000") {
		t.Error("output should contain total bytes 192000")
	}
	// Check table headers
	if !strings.Contains(out, "LCORE") {
		t.Error("output should contain LCORE header")
	}
	if !strings.Contains(out, "PACKETS") {
		t.Error("output should contain PACKETS header")
	}
	if !strings.Contains(out, "BYTES") {
		t.Error("output should contain BYTES header")
	}
	if !strings.Contains(out, "TOTAL") {
		t.Error("output should contain TOTAL row")
	}
}

func TestFormatStatsMonitor(t *testing.T) {
	raw := json.RawMessage(`{
		"threads":[{"lcore_id":1,"packets":500,"bytes":32000}],
		"total":{"packets":500,"bytes":32000}
	}`)
	ts := time.Date(2025, 1, 15, 10, 30, 0, 0, time.UTC)
	out, err := FormatStatsMonitor(raw, ts)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	// Check ANSI clear screen prefix
	if !strings.HasPrefix(out, "\033[2J\033[H") {
		t.Error("output should start with ANSI clear-screen codes")
	}
	// Check timestamp
	if !strings.Contains(out, "2025-01-15T10:30:00Z") {
		t.Error("output should contain the timestamp")
	}
	// Check stats content
	if !strings.Contains(out, "500") {
		t.Error("output should contain packets value")
	}
	if !strings.Contains(out, "32000") {
		t.Error("output should contain bytes value")
	}
}

func TestFormatJSON(t *testing.T) {
	raw := json.RawMessage(`{"key":"value","num":42}`)
	out, err := FormatJSON(raw)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	// Should be valid JSON
	var parsed map[string]interface{}
	if err := json.Unmarshal([]byte(strings.TrimSpace(out)), &parsed); err != nil {
		t.Fatalf("output should be valid JSON: %v", err)
	}
	// Should be indented (multi-line)
	if !strings.Contains(out, "\n") {
		t.Error("output should be indented (multi-line)")
	}
	// Should preserve values
	if parsed["key"] != "value" {
		t.Error("output should preserve key value")
	}
	if parsed["num"] != float64(42) {
		t.Error("output should preserve num value")
	}
}

func TestFormatJSONTrailingNewline(t *testing.T) {
	raw := json.RawMessage(`{"a":1}`)
	out, err := FormatJSON(raw)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !strings.HasSuffix(out, "\n") {
		t.Error("output should end with a newline")
	}
}
func TestFormatCommands(t *testing.T) {
	raw := json.RawMessage(`{"commands":[{"name":"shutdown","tag":"common"},{"name":"get_flow_table","tag":"five_tuple_forwarding"}]}`)
	out, err := FormatCommands(raw)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !strings.Contains(out, "COMMAND") {
		t.Error("output should contain COMMAND header")
	}
	if !strings.Contains(out, "TAG") {
		t.Error("output should contain TAG header")
	}
	if !strings.Contains(out, "shutdown") {
		t.Error("output should contain command name shutdown")
	}
	if !strings.Contains(out, "common") {
		t.Error("output should contain tag common")
	}
	if !strings.Contains(out, "get_flow_table") {
		t.Error("output should contain command name get_flow_table")
	}
	if !strings.Contains(out, "five_tuple_forwarding") {
		t.Error("output should contain tag five_tuple_forwarding")
	}
}

func TestFormatCommandsEmpty(t *testing.T) {
	raw := json.RawMessage(`{"commands":[]}`)
	out, err := FormatCommands(raw)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !strings.Contains(out, "COMMAND") {
		t.Error("output should contain COMMAND header even with empty list")
	}
	if !strings.Contains(out, "TAG") {
		t.Error("output should contain TAG header even with empty list")
	}
}

func TestFormatCommandsInvalidJSON(t *testing.T) {
	raw := json.RawMessage(`{invalid}`)
	_, err := FormatCommands(raw)
	if err == nil {
		t.Error("expected error for invalid JSON")
	}
}
