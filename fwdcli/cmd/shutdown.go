package cmd

import (
	"fmt"
	"os"

	"fwdcli/client"

	"github.com/spf13/cobra"
)

var shutdownCmd = &cobra.Command{
	Use:   "shutdown",
	Short: "Gracefully shut down the DPDK application",
	Long:  "Sends a shutdown command to the DPDK control plane to gracefully stop the process.",
	RunE: func(cmd *cobra.Command, args []string) error {
		c := client.New(socketPath, client.DefaultTimeout)
		if err := c.Connect(); err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(ExitConnError)
		}
		defer c.Close()

		resp, err := c.Send("shutdown")
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(ExitConnError)
		}

		if !resp.IsSuccess() {
			fmt.Fprintf(os.Stderr, "Error: %s\n", resp.Error)
			os.Exit(ExitServerError)
		}

		fmt.Println("Shutdown command sent successfully.")
		return nil
	},
}

func init() {
	rootCmd.AddCommand(shutdownCmd)
}
