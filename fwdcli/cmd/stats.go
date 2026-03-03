package cmd

import (
	"fmt"
	"os"

	"fwdcli/client"
	"fwdcli/formatter"

	"github.com/spf13/cobra"
)

var statsCmd = &cobra.Command{
	Use:   "stats",
	Short: "View per-thread and total packet/byte statistics",
	Long:  "Sends a get_stats command to the DPDK control plane and displays per-thread packets and bytes, and the total packets and bytes.",
	RunE: func(cmd *cobra.Command, args []string) error {
		c := client.New(socketPath, client.DefaultTimeout)
		if err := c.Connect(); err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(ExitConnError)
		}
		defer c.Close()

		resp, err := c.Send("get_stats")
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(ExitConnError)
		}

		if !resp.IsSuccess() {
			fmt.Fprintf(os.Stderr, "Error: %s\n", resp.Error)
			os.Exit(ExitServerError)
		}

		var output string
		if jsonOutput {
			output, err = formatter.FormatJSON(resp.Result)
		} else {
			output, err = formatter.FormatStats(resp.Result)
		}
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(ExitServerError)
		}

		fmt.Print(output)
		return nil
	},
}

func init() {
	rootCmd.AddCommand(statsCmd)
}
