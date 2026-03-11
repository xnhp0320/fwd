package cmd

import (
	"fmt"
	"os"

	"fwdcli/client"
	"fwdcli/formatter"

	"github.com/spf13/cobra"
)

var sessionsCmd = &cobra.Command{
	Use:   "sessions",
	Short: "Display active session table entries",
	RunE: func(cmd *cobra.Command, args []string) error {
		c := client.New(socketPath, client.DefaultTimeout)
		if err := c.Connect(); err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(ExitConnError)
		}
		defer c.Close()

		command := "get_sessions"
		if briefOutput {
			command = "get_sessions_count"
		}

		resp, err := c.Send(command)
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
		} else if briefOutput {
			output, err = formatter.FormatSessionsCount(resp.Result)
		} else {
			output, err = formatter.FormatSessions(resp.Result)
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
	showCmd.AddCommand(sessionsCmd)
}
