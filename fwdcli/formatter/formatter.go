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

// SessionInfo represents a single session entry.
type SessionInfo struct {
	SrcIP     string `json:"src_ip"`
	DstIP     string `json:"dst_ip"`
	SrcPort   int    `json:"src_port"`
	DstPort   int    `json:"dst_port"`
	Protocol  int    `json:"protocol"`
	ZoneID    int    `json:"zone_id"`
	IsIPv6    bool   `json:"is_ipv6"`
	Version   int    `json:"version"`
	Timestamp uint64 `json:"timestamp"`
}

// SessionsResult maps the get_sessions command result.
type SessionsResult struct {
	Sessions []SessionInfo `json:"sessions"`
}
// FlowEntry represents a single flow table entry.
type FlowEntry struct {
	SrcIP    string `json:"src_ip"`
	DstIP    string `json:"dst_ip"`
	SrcPort  int    `json:"src_port"`
	DstPort  int    `json:"dst_port"`
	Protocol int    `json:"protocol"`
	VNI      int    `json:"vni"`
	IsIPv6   bool   `json:"is_ipv6"`
}

// FlowThread represents flow entries for a single PMD thread.
type FlowThread struct {
	LcoreID int         `json:"lcore_id"`
	Entries []FlowEntry `json:"entries"`
}

// FlowsResult maps the get_flow_table command result.
type FlowsResult struct {
	Threads []FlowThread `json:"threads"`
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

// FormatSessions renders a get_sessions response as a human-readable table.
func FormatSessions(result json.RawMessage) (string, error) {
	var s SessionsResult
	if err := json.Unmarshal(result, &s); err != nil {
		return "", fmt.Errorf("failed to parse sessions result: %w", err)
	}

	if len(s.Sessions) == 0 {
		return "No active sessions.\n", nil
	}

	var b strings.Builder
	fmt.Fprintf(&b, "%-20s %-20s %-10s %-10s %-6s %-6s %-6s %-8s %s\n",
		"SRC_IP", "DST_IP", "SRC_PORT", "DST_PORT", "PROTO", "ZONE", "IPV6", "VERSION", "TIMESTAMP")
	fmt.Fprintf(&b, "%-20s %-20s %-10s %-10s %-6s %-6s %-6s %-8s %s\n",
		"------", "------", "--------", "--------", "-----", "----", "----", "-------", "---------")
	for _, sess := range s.Sessions {
		ipv6Str := "false"
		if sess.IsIPv6 {
			ipv6Str = "true"
		}
		fmt.Fprintf(&b, "%-20s %-20s %-10d %-10d %-6d %-6d %-6s %-8d %d\n",
			sess.SrcIP, sess.DstIP, sess.SrcPort, sess.DstPort,
			sess.Protocol, sess.ZoneID, ipv6Str, sess.Version, sess.Timestamp)
	}
	return b.String(), nil
}
// ProcThreadStats represents per-thread processor miss statistics.
type ProcThreadStats struct {
	LcoreID              int    `json:"lcore_id"`
	FlowTableMisses      uint64 `json:"flow_table_misses"`
	SessionLookupMisses  uint64 `json:"session_lookup_misses"`
}

// ProcTotalStats represents aggregate processor miss statistics.
type ProcTotalStats struct {
	FlowTableMisses     uint64 `json:"flow_table_misses"`
	SessionLookupMisses uint64 `json:"session_lookup_misses"`
}

// ProcStatsResult maps the get_proc_stats command result.
type ProcStatsResult struct {
	Threads []ProcThreadStats `json:"threads"`
	Total   ProcTotalStats    `json:"total"`
}

// FormatProcStats renders a get_proc_stats response as a tabular string.
func FormatProcStats(result json.RawMessage) (string, error) {
	var s ProcStatsResult
	if err := json.Unmarshal(result, &s); err != nil {
		return "", fmt.Errorf("failed to parse proc_stats result: %w", err)
	}

	var b strings.Builder
	fmt.Fprintf(&b, "%-10s %20s %24s\n", "LCORE", "FLOW_TABLE_MISSES", "SESSION_LOOKUP_MISSES")
	fmt.Fprintf(&b, "%-10s %20s %24s\n", "-----", "-----------------", "---------------------")
	for _, th := range s.Threads {
		fmt.Fprintf(&b, "%-10d %20d %24d\n", th.LcoreID, th.FlowTableMisses, th.SessionLookupMisses)
	}
	fmt.Fprintf(&b, "%-10s %20s %24s\n", "-----", "-----------------", "---------------------")
	fmt.Fprintf(&b, "%-10s %20d %24d\n", "TOTAL", s.Total.FlowTableMisses, s.Total.SessionLookupMisses)
	return b.String(), nil
}

// SessionsCountResult maps the get_sessions_count command result.
type SessionsCountResult struct {
	Count int `json:"count"`
}

// FlowsCountThread represents flow entry count for a single PMD thread.
type FlowsCountThread struct {
	LcoreID int `json:"lcore_id"`
	Count   int `json:"count"`
}

// FlowsCountResult maps the get_flow_table_count command result.
type FlowsCountResult struct {
	Threads []FlowsCountThread `json:"threads"`
}

// FormatSessionsCount renders only the session count from get_sessions_count.
func FormatSessionsCount(result json.RawMessage) (string, error) {
	var s SessionsCountResult
	if err := json.Unmarshal(result, &s); err != nil {
		return "", fmt.Errorf("failed to parse sessions count result: %w", err)
	}
	return fmt.Sprintf("Sessions: %d\n", s.Count), nil
}

// FormatFlowsCount renders only the per-thread and total flow entry counts from get_flow_table_count.
func FormatFlowsCount(result json.RawMessage) (string, error) {
	var f FlowsCountResult
	if err := json.Unmarshal(result, &f); err != nil {
		return "", fmt.Errorf("failed to parse flow table count result: %w", err)
	}

	var b strings.Builder
	total := 0
	for _, t := range f.Threads {
		fmt.Fprintf(&b, "Lcore %d: %d entries\n", t.LcoreID, t.Count)
		total += t.Count
	}
	fmt.Fprintf(&b, "Total: %d entries\n", total)
	return b.String(), nil
}

// FormatSessionsBrief renders only the session count.
func FormatSessionsBrief(result json.RawMessage) (string, error) {
	var s SessionsResult
	if err := json.Unmarshal(result, &s); err != nil {
		return "", fmt.Errorf("failed to parse sessions result: %w", err)
	}
	return fmt.Sprintf("Sessions: %d\n", len(s.Sessions)), nil
}

// FormatFlowsBrief renders only the per-thread and total flow entry counts.
func FormatFlowsBrief(result json.RawMessage) (string, error) {
	var f FlowsResult
	if err := json.Unmarshal(result, &f); err != nil {
		return "", fmt.Errorf("failed to parse flow table result: %w", err)
	}

	var b strings.Builder
	total := 0
	for _, t := range f.Threads {
		fmt.Fprintf(&b, "Lcore %d: %d entries\n", t.LcoreID, len(t.Entries))
		total += len(t.Entries)
	}
	fmt.Fprintf(&b, "Total: %d entries\n", total)
	return b.String(), nil
}

// FormatFlows renders a get_flow_table response as a human-readable table grouped by thread.
func FormatFlows(result json.RawMessage) (string, error) {
	var f FlowsResult
	if err := json.Unmarshal(result, &f); err != nil {
		return "", fmt.Errorf("failed to parse flow table result: %w", err)
	}

	var b strings.Builder
	totalEntries := 0
	for _, t := range f.Threads {
		totalEntries += len(t.Entries)
	}

	if totalEntries == 0 {
		return "No flow table entries.\n", nil
	}

	for _, t := range f.Threads {
		fmt.Fprintf(&b, "Lcore %d (%d entries):\n", t.LcoreID, len(t.Entries))
		if len(t.Entries) == 0 {
			fmt.Fprintf(&b, "  (empty)\n")
			continue
		}
		fmt.Fprintf(&b, "  %-20s %-20s %-10s %-10s %-6s %-8s %s\n",
			"SRC_IP", "DST_IP", "SRC_PORT", "DST_PORT", "PROTO", "VNI", "IPV6")
		fmt.Fprintf(&b, "  %-20s %-20s %-10s %-10s %-6s %-8s %s\n",
			"------", "------", "--------", "--------", "-----", "---", "----")
		for _, e := range t.Entries {
			ipv6Str := "false"
			if e.IsIPv6 {
				ipv6Str = "true"
			}
			fmt.Fprintf(&b, "  %-20s %-20s %-10d %-10d %-6d %-8d %s\n",
				e.SrcIP, e.DstIP, e.SrcPort, e.DstPort, e.Protocol, e.VNI, ipv6Str)
		}
		b.WriteString("\n")
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

// FormatStatsMonitorWithPPS renders stats with a PPS column computed from the delta
// between the current and previous snapshots over the given interval (seconds).
// Returns the formatted output and the parsed current stats for use as the next "prev".
func FormatStatsMonitorWithPPS(result json.RawMessage, ts time.Time, prev *StatsResult, intervalSec int) (string, *StatsResult, error) {
	var s StatsResult
	if err := json.Unmarshal(result, &s); err != nil {
		return "", nil, fmt.Errorf("failed to parse stats result: %w", err)
	}

	// Build per-lcore PPS map and total PPS
	threadPPS := make(map[int]uint64)
	var totalPPS uint64
	if prev != nil && intervalSec > 0 {
		prevByLcore := make(map[int]uint64)
		for _, t := range prev.Threads {
			prevByLcore[t.LcoreID] = t.Packets
		}
		for _, t := range s.Threads {
			if prevPkts, ok := prevByLcore[t.LcoreID]; ok && t.Packets >= prevPkts {
				threadPPS[t.LcoreID] = (t.Packets - prevPkts) / uint64(intervalSec)
			}
		}
		if s.Total.Packets >= prev.Total.Packets {
			totalPPS = (s.Total.Packets - prev.Total.Packets) / uint64(intervalSec)
		}
	}

	var b strings.Builder
	b.WriteString("\033[2J\033[H")
	fmt.Fprintf(&b, "Stats at %s\n\n", ts.Format(time.RFC3339))

	fmt.Fprintf(&b, "%-10s %15s %15s %12s\n", "LCORE", "PACKETS", "BYTES", "PPS")
	fmt.Fprintf(&b, "%-10s %15s %15s %12s\n", "-----", "-------", "-----", "---")
	for _, th := range s.Threads {
		ppsStr := "-"
		if prev != nil {
			ppsStr = fmt.Sprintf("%d", threadPPS[th.LcoreID])
		}
		fmt.Fprintf(&b, "%-10d %15d %15d %12s\n", th.LcoreID, th.Packets, th.Bytes, ppsStr)
	}
	fmt.Fprintf(&b, "%-10s %15s %15s %12s\n", "-----", "-------", "-----", "---")
	totalPPSStr := "-"
	if prev != nil {
		totalPPSStr = fmt.Sprintf("%d", totalPPS)
	}
	fmt.Fprintf(&b, "%-10s %15d %15d %12s\n", "TOTAL", s.Total.Packets, s.Total.Bytes, totalPPSStr)

	return b.String(), &s, nil
}

// FibInfoResult maps the get_fib_info command result.
type FibInfoResult struct {
	RulesCount  uint64 `json:"rules_count"`
	MaxRules    uint64 `json:"max_rules"`
	NumberTbl8s uint64 `json:"number_tbl8s"`
	MemoryBytes uint64 `json:"memory_bytes"`
}

// FormatFibInfo renders a get_fib_info response as a human-readable string.
func FormatFibInfo(result json.RawMessage) (string, error) {
	var f FibInfoResult
	if err := json.Unmarshal(result, &f); err != nil {
		return "", fmt.Errorf("failed to parse fib_info result: %w", err)
	}

	var b strings.Builder
	fmt.Fprintf(&b, "FIB Information:\n")
	fmt.Fprintf(&b, "  Rules loaded:   %d\n", f.RulesCount)
	fmt.Fprintf(&b, "  Max rules:      %d\n", f.MaxRules)
	fmt.Fprintf(&b, "  Number tbl8s:   %d\n", f.NumberTbl8s)
	fmt.Fprintf(&b, "  Memory usage:   %s\n", formatBytes(f.MemoryBytes))
	return b.String(), nil
}

// formatBytes converts a byte count to a human-readable string.
func formatBytes(bytes uint64) string {
	const (
		kib = 1024
		mib = 1024 * kib
		gib = 1024 * mib
	)
	switch {
	case bytes >= gib:
		return fmt.Sprintf("%.2f GiB", float64(bytes)/float64(gib))
	case bytes >= mib:
		return fmt.Sprintf("%.2f MiB", float64(bytes)/float64(mib))
	case bytes >= kib:
		return fmt.Sprintf("%.2f KiB", float64(bytes)/float64(kib))
	default:
		return fmt.Sprintf("%d B", bytes)
	}
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
