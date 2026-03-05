package formatter

import (
	"encoding/json"
	"fmt"
	"strings"
	"time"
)

// StatusResult maps the status command result.
type StatusResult struct {
	MainLcore     int `json:"main_lcore"`
	NumPmdThreads int `json:"num_pmd_threads"`
	UptimeSeconds int `json:"uptime_seconds"`
}

// ThreadInfo represents a single PMD thread entry.
type ThreadInfo struct {
	LcoreID int `json:"lcore_id"`
}

// ThreadsResult maps the get_threads command result.
type ThreadsResult struct {
	Threads []ThreadInfo `json:"threads"`
}

// ThreadStats represents per-thread statistics.
type ThreadStats struct {
	LcoreID int    `json:"lcore_id"`
	Packets uint64 `json:"packets"`
	Bytes   uint64 `json:"bytes"`
}

// TotalStats represents aggregate statistics.
type TotalStats struct {
	Packets uint64 `json:"packets"`
	Bytes   uint64 `json:"bytes"`
}

// StatsResult maps the get_stats command result.
type StatsResult struct {
	Threads []ThreadStats `json:"threads"`
	Total   TotalStats    `json:"total"`
}
// CommandInfo represents a single command entry.
type CommandInfo struct {
	Name string `json:"name"`
	Tag  string `json:"tag"`
}

// CommandsResult maps the list_commands command result.
type CommandsResult struct {
	Commands []CommandInfo `json:"commands"`
}

// FormatStatus renders a status response as a human-readable string.
func FormatStatus(result json.RawMessage) (string, error) {
	var s StatusResult
	if err := json.Unmarshal(result, &s); err != nil {
		return "", fmt.Errorf("failed to parse status result: %w", err)
	}

	var b strings.Builder
	fmt.Fprintf(&b, "Main Lcore:      %d\n", s.MainLcore)
	fmt.Fprintf(&b, "PMD Threads:     %d\n", s.NumPmdThreads)
	fmt.Fprintf(&b, "Uptime (seconds): %d\n", s.UptimeSeconds)
	return b.String(), nil
}

// FormatThreads renders a get_threads response as a human-readable list.
func FormatThreads(result json.RawMessage) (string, error) {
	var t ThreadsResult
	if err := json.Unmarshal(result, &t); err != nil {
		return "", fmt.Errorf("failed to parse threads result: %w", err)
	}

	var b strings.Builder
	fmt.Fprintf(&b, "PMD Threads (%d):\n", len(t.Threads))
	for _, th := range t.Threads {
		fmt.Fprintf(&b, "  Lcore ID: %d\n", th.LcoreID)
	}
	return b.String(), nil
}

// FormatStats renders a get_stats response as a tabular string.
func FormatStats(result json.RawMessage) (string, error) {
	var s StatsResult
	if err := json.Unmarshal(result, &s); err != nil {
		return "", fmt.Errorf("failed to parse stats result: %w", err)
	}

	return renderStatsTable(&s), nil
}
// FormatCommands renders a list_commands response as a table with COMMAND and TAG columns.
func FormatCommands(result json.RawMessage) (string, error) {
	var c CommandsResult
	if err := json.Unmarshal(result, &c); err != nil {
		return "", fmt.Errorf("failed to parse commands result: %w", err)
	}

	var b strings.Builder
	fmt.Fprintf(&b, "%-30s %s\n", "COMMAND", "TAG")
	fmt.Fprintf(&b, "%-30s %s\n", "-------", "---")
	for _, cmd := range c.Commands {
		fmt.Fprintf(&b, "%-30s %s\n", cmd.Name, cmd.Tag)
	}
	return b.String(), nil
}

// FormatStatsMonitor renders stats with a timestamp header and ANSI clear-screen prefix.
func FormatStatsMonitor(result json.RawMessage, ts time.Time) (string, error) {
	var s StatsResult
	if err := json.Unmarshal(result, &s); err != nil {
		return "", fmt.Errorf("failed to parse stats result: %w", err)
	}

	var b strings.Builder
	// ANSI clear screen and move cursor to top-left
	b.WriteString("\033[2J\033[H")
	fmt.Fprintf(&b, "Stats at %s\n\n", ts.Format(time.RFC3339))
	b.WriteString(renderStatsTable(&s))
	return b.String(), nil
}

// FormatJSON pretty-prints raw JSON for --json mode.
func FormatJSON(raw json.RawMessage) (string, error) {
	pretty, err := json.MarshalIndent(raw, "", "  ")
	if err != nil {
		return "", fmt.Errorf("failed to format JSON: %w", err)
	}
	return string(pretty) + "\n", nil
}

// renderStatsTable builds the tabular output for stats data.
func renderStatsTable(s *StatsResult) string {
	var b strings.Builder
	fmt.Fprintf(&b, "%-10s %15s %15s\n", "LCORE", "PACKETS", "BYTES")
	fmt.Fprintf(&b, "%-10s %15s %15s\n", "-----", "-------", "-----")
	for _, th := range s.Threads {
		fmt.Fprintf(&b, "%-10d %15d %15d\n", th.LcoreID, th.Packets, th.Bytes)
	}
	fmt.Fprintf(&b, "%-10s %15s %15s\n", "-----", "-------", "-----")
	fmt.Fprintf(&b, "%-10s %15d %15d\n", "TOTAL", s.Total.Packets, s.Total.Bytes)
	return b.String()
}
