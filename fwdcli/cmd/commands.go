package cmd

import (
	"fmt"
	"os"

	"fwdcli/client"
	"fwdcli/formatter"

	"github.com/spf13/cobra"
)

var commandsTag string

var commandsCmd = &cobra.Command{
	Use:   "commands",
	Short: "List available control-plane commands",
	Long:  "Sends a list_commands request to the DPDK control plane and displays available commands with their tags.",
	RunE: func(cmd *cobra.Command, args []string) error {
		c := client.New(socketPath, client.DefaultTimeout)
		if err := c.Connect(); err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(ExitConnError)
		}
		defer c.Close()

		var params map[string]string
		if commandsTag != "" {
			params = map[string]string{"tag": commandsTag}
		}

		resp, err := c.SendWithParams("list_commands", params)
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
			output, err = formatter.FormatCommands(resp.Result)
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
	commandsCmd.Flags().StringVar(&commandsTag, "tag", "", "filter commands by tag")
	rootCmd.AddCommand(commandsCmd)
}
