package cmd

import (
	"github.com/spf13/cobra"
)

var showCmd = &cobra.Command{
	Use:   "show",
	Short: "Show various runtime information",
}

func init() {
	rootCmd.AddCommand(showCmd)
}
