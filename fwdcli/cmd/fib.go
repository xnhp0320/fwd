package cmd

import (
	"fmt"
	"os"

	"fwdcli/client"
	"fwdcli/formatter"

	"github.com/spf13/cobra"
)

var fibCmd = &cobra.Command{
	Use:   "fib",
	Short: "Display FIB (Forwarding Information Base) information",
	RunE: func(cmd *cobra.Command, args []string) error {
		c := client.New(socketPath, client.DefaultTimeout)
		if err := c.Connect(); err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(ExitConnError)
		}
		defer c.Close()

		resp, err := c.Send("get_fib_info")
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
			output, err = formatter.FormatFibInfo(resp.Result)
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
	showCmd.AddCommand(fibCmd)
}
