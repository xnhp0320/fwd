package cmd

import (
	"github.com/spf13/cobra"
)

var (
	briefOutput bool // --brief flag for show subcommands
)

var showCmd = &cobra.Command{
	Use:   "show",
	Short: "Show various runtime information",
}

func init() {
	showCmd.PersistentFlags().BoolVar(&briefOutput, "brief", false, "show only table size, not individual entries")
	rootCmd.AddCommand(showCmd)
}
