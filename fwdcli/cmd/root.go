package cmd

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
)

// Exit code constants for consistent CLI exit behavior.
const (
	ExitSuccess    = 0
	ExitConnError  = 1
	ExitUsageError = 2
	ExitServerError = 3
)

var (
	socketPath string // --socket flag
	jsonOutput bool   // --json flag
	version    string // injected via ldflags / x_defs at build time
)

var rootCmd = &cobra.Command{
	Use:   "fwdcli",
	Short: "CLI for the DPDK forwarding application control plane",
	Long:  "fwdcli communicates with the DPDK forwarding application over a Unix domain socket to query status, statistics, and manage the process.",
	Run: func(cmd *cobra.Command, args []string) {
		showVersion, _ := cmd.Flags().GetBool("version")
		if showVersion {
			if version == "" {
				version = "dev"
			}
			fmt.Println(version)
			return
		}
		cmd.Help()
	},
}

func init() {
	rootCmd.PersistentFlags().StringVar(&socketPath, "socket", "/tmp/dpdk_control.sock", "path to the Unix domain socket")
	rootCmd.PersistentFlags().BoolVar(&jsonOutput, "json", false, "output raw JSON instead of human-readable text")
	rootCmd.Flags().Bool("version", false, "print version and exit")
}

// Execute runs the root cobra command. Called from main().
func Execute() {
	if err := rootCmd.Execute(); err != nil {
		os.Exit(ExitUsageError)
	}
}
